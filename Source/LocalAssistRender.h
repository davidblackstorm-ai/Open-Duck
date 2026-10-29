#pragma once
#include "LocalAssist.h"
class OpenDuckProcessor;
namespace odx::ai
{
Context makeContext(const OpenDuckProcessor &, int mode, std::uint64_t revision);
bool renderCandidate(int mode, const Candidate &, const CaptureView &, const Context &,
                     Metrics &, const std::atomic<bool> &cancel);
struct StereoRenderAudit { Metrics measured, averageReference; double maximumChannelDifference=0; };
bool auditCandidateStereo(int mode,const Candidate &,const CaptureView &,const Context &,
                          StereoRenderAudit &,const std::atomic<bool> &cancel);
// Applying and grouping Undo is intentionally owned by the live processor.
}
