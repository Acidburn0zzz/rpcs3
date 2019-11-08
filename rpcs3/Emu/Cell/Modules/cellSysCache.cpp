﻿#include "stdafx.h"
#include "Emu/System.h"
#include "Emu/IdManager.h"
#include "Emu/Cell/PPUModule.h"

#include "cellSysutil.h"
#include "util/init_mutex.hpp"
#include "Utilities/StrUtil.h"

extern logs::channel cellSysutil;

template<>
void fmt_class_string<CellSysCacheError>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](auto error)
	{
		switch (error)
		{
			STR_CASE(CELL_SYSCACHE_ERROR_ACCESS_ERROR);
			STR_CASE(CELL_SYSCACHE_ERROR_INTERNAL);
			STR_CASE(CELL_SYSCACHE_ERROR_NOTMOUNTED);
			STR_CASE(CELL_SYSCACHE_ERROR_PARAM);
		}

		return unknown;
	});
}

struct syscache_info
{
	const std::string cache_root = Emu.GetHdd1Dir() + "/caches/";

	stx::init_mutex init;

	std::string cache_id;

	syscache_info() noexcept
	{
		// Find existing cache at startup
		const std::string prefix = Emu.GetTitleID() + '_';

		for (auto&& entry : fs::dir(cache_root))
		{
			if (entry.is_directory && entry.name.size() >= prefix.size() && entry.name.compare(0, prefix.size(), prefix) == 0)
			{
				cache_id = std::move(entry.name);
				break;
			}
		}
	}

	void clear(bool remove_root) noexcept
	{
		// Clear cache
		if (!vfs::host::remove_all(cache_root + cache_id, cache_root, remove_root))
		{
			cellSysutil.fatal("cellSysCache: failed to clear cache directory '%s%s' (%s)", cache_root, cache_id, fs::g_tls_error);
		}

		if (!remove_root)
		{
			// Recreate /cache subdirectory if necessary
			fs::create_path(cache_root + cache_id + "/cache");
		}
	}
};

error_code cellSysCacheClear()
{
	cellSysutil.notice("cellSysCacheClear()");

	const auto cache = g_fxo->get<syscache_info>();

	const auto lock = cache->init.access();

	if (!lock)
	{
		return CELL_SYSCACHE_ERROR_NOTMOUNTED;
	}

	// Clear existing cache
	if (!cache->cache_id.empty())
	{
		cache->clear(false);
	}

	return not_an_error(CELL_SYSCACHE_RET_OK_CLEARED);
}

error_code cellSysCacheMount(vm::ptr<CellSysCacheParam> param)
{
	cellSysutil.notice("cellSysCacheMount(param=*0x%x)", param);

	const auto cache = g_fxo->get<syscache_info>();

	if (!param || !std::memchr(param->cacheId, '\0', CELL_SYSCACHE_ID_SIZE))
	{
		return CELL_SYSCACHE_ERROR_PARAM;
	}

	// Full virtualized cache id (with title id included)
	std::string cache_id = vfs::escape(Emu.GetTitleID() + '_' + param->cacheId, true);

	// Full path to virtual cache root (/dev_hdd1)
	std::string new_path = cache->cache_root + cache_id + '/';

	// Set fixed VFS path
	strcpy_trunc(param->getCachePath, "/dev_hdd1/cache");

	// Lock pseudo-mutex
	const auto lock = cache->init.init_always([&]
	{
	});

	// Check if can reuse existing cache (won't if cache id is an empty string)
	if (param->cacheId[0] && cache_id == cache->cache_id && fs::is_dir(new_path + "cache"))
	{
		// Isn't mounted yet on first call to cellSysCacheMount
		vfs::mount("/dev_hdd1", new_path);

		cellSysutil.success("Mounted existing cache at %s", new_path);
		return not_an_error(CELL_SYSCACHE_RET_OK_RELAYED);
	}

	// Clear existing cache
	if (!cache->cache_id.empty())
	{
		cache->clear(true);
	}

	// Set new cache id
	cache->cache_id = std::move(cache_id);
	fs::create_path(new_path + "cache");
	vfs::mount("/dev_hdd1", new_path);

	return not_an_error(CELL_SYSCACHE_RET_OK_CLEARED);
}


extern void cellSysutil_SysCache_init()
{
	REG_FUNC(cellSysutil, cellSysCacheMount);
	REG_FUNC(cellSysutil, cellSysCacheClear);
}