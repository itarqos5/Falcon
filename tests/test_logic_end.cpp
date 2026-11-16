// The ten pillars of the End (world/nether_decoration.hpp).
#include "falcon/world/nether_decoration.hpp"
#include "test_logic.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace falcon;

void testEndSpikes() {
    const auto spikes = endSpikes(12345);
    std::vector<int> heights, radii;
    int guarded = 0;
    for (const EndSpike& s : spikes) {
        heights.push_back(s.height);
        radii.push_back(s.radius);
        guarded += s.guarded ? 1 : 0;
        // On a ring of about 42 blocks.
        const double distance = std::sqrt(static_cast<double>(s.centerX * s.centerX + s.centerZ * s.centerZ));
        LCHECK(distance > 40.0 && distance < 43.0);
    }
    std::sort(heights.begin(), heights.end());
    for (int i = 0; i < 10; ++i) LCHECK(heights[static_cast<std::size_t>(i)] == 76 + 3 * i);  // 76, 79 ... 103, each once
    std::sort(radii.begin(), radii.end());
    LCHECK(radii.front() == 2 && radii.back() == 5);
    LCHECK(guarded == 2);  // two pillars have cages
    // The seed shuffles which is which, but the same seed gives the same End.
    const auto again = endSpikes(12345);
    for (std::size_t i = 0; i < spikes.size(); ++i) LCHECK(spikes[i].height == again[i].height);
}
