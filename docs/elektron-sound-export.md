# Elektron sound package export

Sound tags are optional. An empty `sound_tags` array must not prevent reading
`sample_references` from the device metadata. Referenced samples are still
resolved, downloaded and included as WAV resources in the exported package.

Samples that cannot be resolved or downloaded may still be omitted; an empty
tag list alone is not a reason to omit them. Export does not delete samples
from the device.

The offline Elektron regression test checks that empty and non-empty tag arrays
leave the JSON reader positioned to read sample references. It does not replace
a hardware export/import test.
