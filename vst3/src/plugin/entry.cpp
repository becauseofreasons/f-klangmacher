#include "ids.h"
#include "processor.h"
#include "controller.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF("BecauseOfReasons", "", "")

  DEF_CLASS2(INLINE_UID_FROM_FUID(smk::ProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
             "F. Klangmacher", Vst::kDistributable, PlugType::kInstrumentSynth, "1.1.0", kVstVersionString,
             smk::Processor::createInstance)

  DEF_CLASS2(INLINE_UID_FROM_FUID(smk::ControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
             "F. Klangmacher Controller", 0, "", "1.1.0", kVstVersionString,
             smk::Controller::createInstance)

END_FACTORY
