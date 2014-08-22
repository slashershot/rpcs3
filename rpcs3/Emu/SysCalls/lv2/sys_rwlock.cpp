#include "stdafx.h"
#include "Utilities/Log.h"
#include "Emu/Cell/PPUThread.h"
#include "Emu/System.h"
#include "Emu/SysCalls/SysCalls.h"
#include "sys_rwlock.h"

SysCallBase sys_rwlock("sys_rwlock");

s32 sys_rwlock_create(mem32_t rw_lock_id, mem_ptr_t<sys_rwlock_attribute_t> attr)
{
	sys_rwlock.Warning("sys_rwlock_create(rw_lock_id_addr=0x%x, attr_addr=0x%x)", rw_lock_id.GetAddr(), attr.GetAddr());

	if (!attr)
		return CELL_EFAULT;

	switch (attr->attr_protocol.ToBE())
	{
	case se(attr->attr_protocol, SYS_SYNC_PRIORITY): sys_rwlock.Todo("SYS_SYNC_PRIORITY"); break;
	case se(attr->attr_protocol, SYS_SYNC_RETRY): sys_rwlock.Error("SYS_SYNC_RETRY"); return CELL_EINVAL;
	case se(attr->attr_protocol, SYS_SYNC_PRIORITY_INHERIT): sys_rwlock.Todo("SYS_SYNC_PRIORITY_INHERIT"); break;
	case se(attr->attr_protocol, SYS_SYNC_FIFO): break;
	default: return CELL_EINVAL;
	}

	if (attr->attr_pshared.ToBE() != se32(0x200))
	{
		sys_rwlock.Error("Invalid attr_pshared(0x%x)", (u32)attr->attr_pshared);
		return CELL_EINVAL;
	}

	rw_lock_id = sys_rwlock.GetNewId(new RWLock((u32)attr->attr_protocol, attr->name_u64), TYPE_RWLOCK);

	sys_rwlock.Warning("*** rwlock created [%s] (protocol=0x%x): id = %d",
		std::string(attr->name, 8).c_str(), (u32)attr->attr_protocol, rw_lock_id.GetValue());

	return CELL_OK;
}

s32 sys_rwlock_destroy(u32 rw_lock_id)
{
	sys_rwlock.Warning("sys_rwlock_destroy(rw_lock_id=%d)", rw_lock_id);

	RWLock* rw;
	if (!sys_rwlock.CheckId(rw_lock_id, rw)) return CELL_ESRCH;

	std::lock_guard<std::mutex> lock(rw->m_lock);

	if (rw->wlock_queue.size() || rw->rlock_list.size() || rw->wlock_thread) return CELL_EBUSY;

	Emu.GetIdManager().RemoveID(rw_lock_id);

	return CELL_OK;
}

s32 sys_rwlock_rlock(u32 rw_lock_id, u64 timeout)
{
	sys_rwlock.Log("sys_rwlock_rlock(rw_lock_id=%d, timeout=%lld)", rw_lock_id, timeout);

	RWLock* rw;
	if (!sys_rwlock.CheckId(rw_lock_id, rw)) return CELL_ESRCH;
	const u32 tid = GetCurrentPPUThread().GetId();

	if (rw->rlock_trylock(tid)) return CELL_OK;

	u32 counter = 0;
	const u32 max_counter = timeout ? (timeout / 1000) : 20000;
	do
	{
		if (Emu.IsStopped())
		{
			LOG_WARNING(HLE, "sys_rwlock_rlock(rw_lock_id=%d, ...) aborted", rw_lock_id);
			return CELL_ETIMEDOUT;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		if (rw->rlock_trylock(tid)) return CELL_OK;

		if (counter++ > max_counter)
		{
			if (!timeout) 
			{
				counter = 0;
			}
			else
			{
				return CELL_ETIMEDOUT;
			}
		}		
	} while (true);
}

s32 sys_rwlock_tryrlock(u32 rw_lock_id)
{
	sys_rwlock.Log("sys_rwlock_tryrlock(rw_lock_id=%d)", rw_lock_id);

	RWLock* rw;
	if (!sys_rwlock.CheckId(rw_lock_id, rw)) return CELL_ESRCH;

	if (!rw->rlock_trylock(GetCurrentPPUThread().GetId())) return CELL_EBUSY;

	return CELL_OK;
}

s32 sys_rwlock_runlock(u32 rw_lock_id)
{
	sys_rwlock.Log("sys_rwlock_runlock(rw_lock_id=%d)", rw_lock_id);

	RWLock* rw;
	if (!sys_rwlock.CheckId(rw_lock_id, rw)) return CELL_ESRCH;

	if (!rw->rlock_unlock(GetCurrentPPUThread().GetId())) return CELL_EPERM;

	return CELL_OK;
}

s32 sys_rwlock_wlock(u32 rw_lock_id, u64 timeout)
{
	sys_rwlock.Log("sys_rwlock_wlock(rw_lock_id=%d, timeout=%lld)", rw_lock_id, timeout);

	RWLock* rw;
	if (!sys_rwlock.CheckId(rw_lock_id, rw)) return CELL_ESRCH;
	const u32 tid = GetCurrentPPUThread().GetId();

	if (!rw->wlock_check(tid)) return CELL_EDEADLK;

	if (rw->wlock_trylock(tid, true)) return CELL_OK;

	u32 counter = 0;
	const u32 max_counter = timeout ? (timeout / 1000) : 20000;
	do
	{
		if (Emu.IsStopped())
		{
			LOG_WARNING(HLE, "sys_rwlock_wlock(rw_lock_id=%d, ...) aborted", rw_lock_id);
			return CELL_ETIMEDOUT;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		if (rw->wlock_trylock(tid, true)) return CELL_OK;

		if (counter++ > max_counter)
		{
			if (!timeout) 
			{
				counter = 0;
			}
			else
			{
				return CELL_ETIMEDOUT;
			}
		}		
	} while (true);
}

s32 sys_rwlock_trywlock(u32 rw_lock_id)
{
	sys_rwlock.Log("sys_rwlock_trywlock(rw_lock_id=%d)", rw_lock_id);

	RWLock* rw;
	if (!sys_rwlock.CheckId(rw_lock_id, rw)) return CELL_ESRCH;
	const u32 tid = GetCurrentPPUThread().GetId();

	if (!rw->wlock_check(tid)) return CELL_EDEADLK;

	if (!rw->wlock_trylock(tid, false)) return CELL_EBUSY;

	return CELL_OK;
}

s32 sys_rwlock_wunlock(u32 rw_lock_id)
{
	sys_rwlock.Log("sys_rwlock_wunlock(rw_lock_id=%d)", rw_lock_id);

	RWLock* rw;
	if (!sys_rwlock.CheckId(rw_lock_id, rw)) return CELL_ESRCH;

	if (!rw->wlock_unlock(GetCurrentPPUThread().GetId())) return CELL_EPERM;

	return CELL_OK;
}
