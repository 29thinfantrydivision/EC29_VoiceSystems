//! Master switch for the [EC29-DBG] diagnostic chain.
//!
//! Release builds ship with VERBOSE = false: the NORMAL-level trace chatter
//! (per-keypress, per-packet, per-squelch logging) compiles down to a dead
//! branch and its format arguments are never evaluated. Warnings and errors
//! that indicate a broken install (missing override, null manager, failed
//! resource load) stay UNGATED on purpose - those must reach every RPT.
//!
//! Flip to true when chasing a bug; the entire original debug chain returns.
//!
//! Currently TRUE for the integration weekend: every RPT captures the full
//! trace chain so field reports come back with usable logs. Flip back to
//! false for the wide release build.
class EC29_Debug
{
	static const bool VERBOSE = true;
}
