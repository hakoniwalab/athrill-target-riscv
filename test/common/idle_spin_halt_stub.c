#include "athrill_idle_spin_halt.h"

void athrill_idle_spin_halt_register_target(const AthrillIdleSpinHaltTargetOps *ops)
{
	(void)ops;
}

std_bool athrill_idle_spin_halt_idle_ready(CoreIdType core_id)
{
	(void)core_id;
	return FALSE;
}

std_bool athrill_idle_spin_halt_before_execute(CoreIdType core_id)
{
	(void)core_id;
	return FALSE;
}

void athrill_idle_spin_halt_after_execute(CoreIdType core_id, AthrillAddrType pc)
{
	(void)core_id;
	(void)pc;
}
