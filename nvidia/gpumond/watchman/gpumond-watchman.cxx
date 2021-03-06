
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <pwd.h>

#include "watchman/plugin.hxx"
#include "watchman/alloc.hxx"
#include "watchman/program.hxx"
#include "watchman/buffer.hxx"
#include "watchman/file.hxx"
#include "watchman/named_unpriv_clingy_file.hxx"
#include "watchman/size_rotator.hxx"
#include "watchman/watchman.hxx"
#include "watchman/compiler.hxx"
#include "watchman/error.hxx"

static char _gpumond[1][WATCHMAN_PROGRAM_MAX_ARGV_STRLEN + 1];
static char *_argv[2];

static char **_fill_argv()
{
	strcpy(_gpumond[0], GPUMOND_BIN_PREFIX "/gpumond.exe");

	_argv[0] = _gpumond[0];
	_argv[1] = nullptr;

	return _argv;
}

int get_uid_and_gid(char *name, int *uid, int *gid)
{
	struct passwd *pwd;

	*uid = 0;
	*gid = 0;

	pwd = getpwnam(name);
	if (unlikely(!pwd)) {
		WATCHMAN_ERROR("getpwnam() failed with errno %d: %s", errno, strerror(errno));
		return -errno;
	}

	*uid = pwd->pw_uid;
	*gid = pwd->pw_gid;

	return 0;
}

class Gpumond_Program : public Program
{

public:
					Gpumond_Program();

};

class Gpumond_Watchman_Plugin : public Watchman_Plugin
{

public:
					explicit Gpumond_Watchman_Plugin(void *handle);

public:
	int				init(Watchman *w, int argc, char **argv);
	int				fini();

private:
	Allocator			*_alloc;

private:
	Gpumond_Program			*_proc;

private:
	Buffer				_buf;

private:
					/* Mountpoint of the filesystem on which the
					 * the output file resides. */
	const char			*_fo_mountpoint;
	const char			*_fo_path;

private:
	Named_Unpriv_Clingy_File	*_fo;
	File				*_fe;

	Size_Rotator			*_rot;
};

Gpumond_Program::Gpumond_Program()
: Program(_fill_argv())
{
}

Gpumond_Watchman_Plugin::Gpumond_Watchman_Plugin(void *handle)
: Watchman_Plugin(handle, 1), _fo(nullptr), _fe(nullptr)
{
}

int Gpumond_Watchman_Plugin::init(Watchman *w, int argc, char **argv)
{
	int       err;
	off_t     offset;
	int       uid, gid;
	long long threshold;

	_alloc = w->alloc();

	err = get_uid_and_gid(argv[0], &uid, &gid);
	if (unlikely(err < 0)) {
		return err;
	}

	_fo_mountpoint = argv[1];
	_fo_path       = argv[2];

	threshold = atoll(argv[3]);

	_proc = _alloc->create<Gpumond_Program>();

	_fo = _alloc->create<Named_Unpriv_Clingy_File>(uid, gid);
	_fe = _alloc->create<File>(STDERR_FILENO);

	err = _fo->attach(_fo_mountpoint);
	if (unlikely(err)) {
		return err;
	}

	err = _fo->open(_fo_path, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (unlikely(err)) {
		return err;
	}

	offset = lseek(_fo->fileno(), 0, SEEK_END);
	if (unlikely(-1 == offset)) {
		WATCHMAN_ERROR("lseek() failed with error %d (%s)", errno, strerror(errno));
	}

	_rot = _alloc->create<Size_Rotator>(threshold);

	err = w->add_child(_proc, &_buf, _fo, _fe, _rot);
	if (unlikely(err)) {
		WATCHMAN_ERROR("Failed to add children to list: %d", err);
		return err;
	}

	return 0;
}

int Gpumond_Watchman_Plugin::fini()
{
	_rot = _alloc->destroy<Size_Rotator>(_rot);

	_fo = _alloc->destroy<Named_Unpriv_Clingy_File>(_fo);
	_fe = _alloc->destroy<File>(_fe);

	_proc = _alloc->destroy<Gpumond_Program>(_proc);

	return 0;
}

extern "C" Watchman_Plugin *entry(void *handle, Watchman *w)
{
	Allocator *alloc = w->alloc();

	return alloc->create<Gpumond_Watchman_Plugin>(handle);
}

