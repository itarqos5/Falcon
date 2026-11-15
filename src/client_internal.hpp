#pragma once

// What the parts of the client (client*.cpp) call across files.
#include "falcon/assets/resource_manager.hpp"

namespace falcon {

int runAudioDevices();                               // --audio-devices: lists the sound devices
int runAudioTest(const ResourceManager& resources);  // --audio-test: plays a sound and checks it was heard

}  // namespace falcon
