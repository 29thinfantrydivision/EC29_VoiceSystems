//! Master switch for the [EC29-DBG] diagnostic chain.
//!
//! Release builds ship with VERBOSE = false: the NORMAL-level trace chatter
//! (per-keypress, per-packet, per-squelch logging) compiles down to a dead
//! branch and its format arguments are never evaluated. Warnings and errors
//! that indicate a broken install (missing override, null manager, failed
//! resource load) stay UNGATED on purpose - those must reach every RPT.
//!
//! Flip to true when chasing a bug; the entire original debug chain returns.
class EC29_Debug
{
	static const bool VERBOSE = false;
}
