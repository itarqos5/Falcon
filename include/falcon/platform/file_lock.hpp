#pragma once

#include <filesystem>

namespace falcon {

enum class LockProbe { Missing, Free, Locked };

// The check the game does on a world's session.lock: Missing if the file cannot be opened for
// writing, Locked if another process holds a lock on it (the world is open in a running game),
// otherwise Free. Takes no lock afterwards.
[[nodiscard]] LockProbe probeSessionLock(const std::filesystem::path& lockFile);

}  // namespace falcon
