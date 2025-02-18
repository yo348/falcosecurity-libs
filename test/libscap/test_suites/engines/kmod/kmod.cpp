#include <scap.h>
#include <gtest/gtest.h>
#include <unordered_set>
#include <helpers/engines.h>
#include <libscap_test_var.h>
#include <syscall.h>
#include <fcntl.h>

int remove_kmod(char* error_buf)
{
	if(syscall(__NR_delete_module, LIBSCAP_TEST_KERNEL_MODULE_NAME, O_NONBLOCK))
	{
		switch(errno)
		{
		case ENOENT:
			return EXIT_SUCCESS;

		/* If a module has a nonzero reference count with `O_NONBLOCK` flag
		 * the call returns immediately, with `EWOULDBLOCK` code. So in that
		 * case we wait until the module is detached.
		 */
		case EWOULDBLOCK:
			for(int i = 0; i < 4; i++)
			{
				int ret = syscall(__NR_delete_module, LIBSCAP_TEST_KERNEL_MODULE_NAME, O_NONBLOCK);
				if(ret == 0 || errno == ENOENT)
				{
					return EXIT_SUCCESS;
				}
				sleep(1);
			}
			snprintf(error_buf, SCAP_LASTERR_SIZE, "could not remove the kernel module");
			return EXIT_FAILURE;

		case EBUSY:
		case EFAULT:
		case EPERM:
			snprintf(error_buf, SCAP_LASTERR_SIZE, "Unable to remove kernel module. Errno message: %s, errno: %d\n", strerror(errno), errno);
			return EXIT_FAILURE;

		default:
			snprintf(error_buf, SCAP_LASTERR_SIZE, "Unexpected error code. Errno message: %s, errno: %d\n", strerror(errno), errno);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

int insert_kmod(const char* kmod_path, char* error_buf)
{
	/* Here we want to insert the module if we fail we need to abort the program. */
	int fd = open(kmod_path, O_RDONLY);
	if(fd < 0)
	{
		snprintf(error_buf, SCAP_LASTERR_SIZE, "Unable to open the kmod file. Errno message: %s, errno: %d\n", strerror(errno), errno);
		return EXIT_FAILURE;
	}

	if(syscall(__NR_finit_module, fd, "", 0))
	{
		snprintf(error_buf, SCAP_LASTERR_SIZE, "Unable to inject the kmod. Errno message: %s, errno: %d\n", strerror(errno), errno);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

scap_t* open_kmod_engine(char* error_buf, int32_t* rc, unsigned long buffer_dim, const char* kmod_path, std::unordered_set<uint32_t> tp_set = {}, std::unordered_set<uint32_t> ppm_sc_set = {})
{
	struct scap_open_args oargs = {
		.engine_name = KMOD_ENGINE,
		.mode = SCAP_MODE_LIVE,
	};

	/* Remove previously inserted kernel module */
	if(remove_kmod(error_buf) != EXIT_SUCCESS)
	{
		return NULL;
	}

	/* Insert again the kernel module */
	if(insert_kmod(kmod_path, error_buf) != EXIT_SUCCESS)
	{
		return NULL;
	}

	/* If empty we fill with all tracepoints */
	if(tp_set.empty())
	{
		for(int i = 0; i < TP_VAL_MAX; i++)
		{
			oargs.tp_of_interest.tp[i] = 1;
		}
	}
	else
	{
		for(auto tp : tp_set)
		{
			oargs.tp_of_interest.tp[tp] = 1;
		}
	}

	/* If empty we fill with all syscalls */
	if(ppm_sc_set.empty())
	{
		for(int i = 0; i < PPM_SC_MAX; i++)
		{
			oargs.ppm_sc_of_interest.ppm_sc[i] = 1;
		}
	}
	else
	{
		for(auto ppm_sc : ppm_sc_set)
		{
			oargs.ppm_sc_of_interest.ppm_sc[ppm_sc] = 1;
		}
	}

	struct scap_kmod_engine_params kmod_params = {
		.buffer_bytes_dim = buffer_dim,
	};
	oargs.engine_params = &kmod_params;

	return scap_open(&oargs, error_buf, rc);
}

TEST(kmod, open_engine)
{
	char error_buffer[SCAP_LASTERR_SIZE] = {0};
	int ret = 0;
	scap_t* h = open_kmod_engine(error_buffer, &ret, 4 * 4096, LIBSCAP_TEST_KERNEL_MODULE_PATH);
	ASSERT_FALSE(!h || ret != SCAP_SUCCESS) << "unable to open kmod engine: " << error_buffer << std::endl;
	scap_close(h);
}

TEST(kmod, wrong_buffer_dim)
{
	char error_buffer[SCAP_LASTERR_SIZE] = {0};
	int ret = 0;
	scap_t* h = open_kmod_engine(error_buffer, &ret, 4, LIBSCAP_TEST_KERNEL_MODULE_PATH);
	ASSERT_TRUE(!h || ret != SCAP_SUCCESS) << "the buffer dimension is not a system page multiple, so we should fail: " << error_buffer << std::endl;
}

/* This check is not so reliable, better than nothing but to be sure we need to obtain the producer and consumer positions from the drivers */
TEST(kmod, events_not_overwritten)
{
	char error_buffer[SCAP_LASTERR_SIZE] = {0};
	int ret = 0;
	scap_t* h = open_kmod_engine(error_buffer, &ret, 4 * 4096, LIBSCAP_TEST_KERNEL_MODULE_PATH);
	ASSERT_FALSE(!h || ret != SCAP_SUCCESS) << "unable to open kmod engine: " << error_buffer << std::endl;

	check_event_is_not_overwritten(h);
	scap_close(h);
}

TEST(kmod, read_in_order)
{
	char error_buffer[SCAP_LASTERR_SIZE] = {0};
	int ret = 0;
	/* We use buffers of 1 MB to be sure that we don't have drops */
	scap_t* h = open_kmod_engine(error_buffer, &ret, 1 * 1024 * 1024, LIBSCAP_TEST_KERNEL_MODULE_PATH);
	ASSERT_FALSE(!h || ret != SCAP_SUCCESS) << "unable to open kmod engine: " << error_buffer << std::endl;

	check_event_order(h);
	scap_close(h);
}
