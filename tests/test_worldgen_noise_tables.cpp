// D:/Projects/Falcon/tests/worldgen tests: noise tables.
#include "test_worldgen_support.hpp"

// =================================================================================================================
// NormalNoise from the game's JSON files                         [REAL: real-classes-groundtruth.txt, sections 3/4]
// Per noise and world seed: Noise.get at P1 = (0,0,0), P2 = (100.5,0,-250.25), P3 = (-1234.567,0,8910.11),
// P4 = (37,-20,-3), as raw float bits. Noise = NormalNoise.create(root.fromHashOf("minecraft:<name>")).
struct NoiseVec {
    const char* name;
    std::uint32_t bits[4];
};
struct OctaveVec {
    int octave;
    std::uint64_t frequency, amplitude;  // doubles
    std::uint32_t floatAmplitude;        // (float)(normalizationFactor * amplitude)
};
struct DerivedVec {
    const char* name;
    std::uint64_t normalizationFactor;
    std::vector<OctaveVec> octaves;
};

const NoiseVec kNoise_12345[] = {
    {"aquifer_barrier", {0x3c4779a0u, 0xbd351164u, 0xbd586268u, 0xbea77703u}},
    {"aquifer_fluid_level_floodedness", {0x3d2d76f0u, 0xbe9aaa7bu, 0x3dd2b06cu, 0x3c9230e8u}},
    {"aquifer_fluid_level_spread", {0xbc31c4eau, 0xbc9ed408u, 0xbe04e908u, 0xbe5e4a17u}},
    {"aquifer_lava", {0x3efefc5fu, 0x3da7df5bu, 0xbe0e2648u, 0x3d812ad0u}},
    {"badlands_pillar", {0xbe14823bu, 0xbe0ad003u, 0x3d53f56eu, 0xbe855b73u}},
    {"badlands_pillar_roof", {0xbee05626u, 0x3de15be6u, 0x3e86c1e7u, 0xbefde410u}},
    {"badlands_surface", {0x3c4d37bcu, 0xbeb4c7c8u, 0x3cc873cau, 0x3c86e349u}},
    {"calcite", {0xbebfd6e5u, 0x3eb7b388u, 0x3e117857u, 0xbea9530cu}},
    {"cave_cheese", {0x3f1cfe2du, 0xbe2c914cu, 0x3ec5e578u, 0x3ea6fc3bu}},
    {"cave_entrance", {0x3cf44bdeu, 0xbe0d8d84u, 0xbe2dd714u, 0x3c80e424u}},
    {"cave_layer", {0xbf35b4afu, 0xbe599fc6u, 0xbc98eeccu, 0xbf5be5e6u}},
    {"clay_bands_offset", {0xbdd0fed4u, 0xbe5bcc36u, 0xbd9ebb8cu, 0x3caa9e64u}},
    {"continentalness", {0xbece23a6u, 0x3ebaaefbu, 0x3d6037f8u, 0xbdcb9af0u}},
    {"continentalness_large", {0xbf057508u, 0x3d9bd5bbu, 0xbf0f50c3u, 0xbef9d1c9u}},
    {"erosion", {0xbf3c0cafu, 0xbde3065du, 0x3ecbc3c2u, 0xbf25c266u}},
    {"erosion_large", {0x3d84d062u, 0xbdae0f50u, 0xbe1ee88eu, 0x3c68d166u}},
    {"gravel", {0xbead62d3u, 0x3c9093b3u, 0xbee7058eu, 0xbd47e8beu}},
    {"gravel_layer", {0x3ef25b04u, 0x3e1a60b1u, 0xbe687efbu, 0x3dc89c08u}},
    {"ice", {0xbeadc54eu, 0x3e651c3fu, 0xbe6ecb6au, 0x3eefa415u}},
    {"iceberg_pillar", {0xbe432835u, 0xbe12b880u, 0x3ef4aac7u, 0xbcb6577cu}},
    {"iceberg_pillar_roof", {0x3e3b5844u, 0xbe6033c3u, 0x3f1c7bfbu, 0xbd963aa6u}},
    {"iceberg_surface", {0x3eb86644u, 0x3c95c304u, 0xbe325930u, 0x3e6cb173u}},
    {"jagged", {0x3eebf5c5u, 0x3ee90a7eu, 0x3f18cfa4u, 0x3ee9c660u}},
    {"nether_state_selector", {0xbd7a7d1cu, 0x3df0833au, 0x3b9ab3c0u, 0x3e2d8d1eu}},
    {"nether_wart", {0x3f3aad4du, 0x3f033975u, 0xbe8f9646u, 0x3e8686a7u}},
    {"netherrack", {0x3cf5a921u, 0x3ece98c4u, 0x3cb0a966u, 0x3ebb0039u}},
    {"noodle", {0x3f22323fu, 0xbf30eb3bu, 0xbe9775eau, 0x3f056f13u}},
    {"noodle_ridge_a", {0xbef83300u, 0xbe8aeaafu, 0x3d36f798u, 0xbe636e30u}},
    {"noodle_ridge_b", {0x3d5fd61cu, 0xbe8446b6u, 0xbe6d6d1eu, 0x3e4e67c3u}},
    {"noodle_thickness", {0x3e851f7eu, 0xbeb1bbb8u, 0x3e50cf18u, 0x3e834652u}},
    {"offset", {0xbf0982fdu, 0x3e4c7738u, 0xbefa653du, 0xbede449bu}},
    {"ore_gap", {0x3ef2c106u, 0xbf113fafu, 0xbdc8f2a9u, 0x3f060618u}},
    {"ore_vein_a", {0xbd8d5c72u, 0xbe621a58u, 0x3e81c1c0u, 0xbe705787u}},
    {"ore_vein_b", {0x3e1e11b7u, 0x3e82be1au, 0x3ef8cebbu, 0xbdabcba4u}},
    {"ore_veininess", {0xbeebde00u, 0x3ee7b3bau, 0xbe039554u, 0xbf1c77a8u}},
    {"packed_ice", {0x3e0d9898u, 0xbeb94bebu, 0x3de948fdu, 0xbe6bfc1cu}},
    {"patch", {0x3ea46e40u, 0xbe2218f7u, 0x3d22366cu, 0xbd4a123bu}},
    {"pillar", {0x3e490be1u, 0xbeeb836au, 0xbe666576u, 0xbe0ba849u}},
    {"pillar_rareness", {0x3e22d23bu, 0x3e02ead9u, 0x3d7f9ab6u, 0x3e8765c8u}},
    {"pillar_thickness", {0x3e98fc05u, 0x3eda7beau, 0xbef5463eu, 0x3eb7ecc7u}},
    {"powder_snow", {0xbc6253e2u, 0x3f3e37d9u, 0x3da833d0u, 0xbdfb3daeu}},
    {"ridge", {0x3e33a71au, 0x3d573586u, 0x3d1ca3d8u, 0x3d9fa654u}},
    {"small_patch", {0x3e5a2018u, 0xbfc48b40u, 0xbdaf3e30u, 0x3f09a692u}},
    {"soul_sand_layer", {0x3ed307b4u, 0x3eec3d7bu, 0xbe95d3e1u, 0x3e367237u}},
    {"spaghetti_2d", {0xbf24b8f5u, 0x3e8b0dafu, 0x3e771f8cu, 0xbec17d7cu}},
    {"spaghetti_2d_elevation", {0x3e97eed0u, 0x3f02ba34u, 0xbc493ec0u, 0x3ef04eb4u}},
    {"spaghetti_2d_modulator", {0x3e1b356fu, 0x3e377c89u, 0xbd04e356u, 0x3e186203u}},
    {"spaghetti_2d_thickness", {0xbee38920u, 0xbe91e345u, 0x3d97a4dcu, 0xbedbeaffu}},
    {"spaghetti_3d_1", {0x3b6939b7u, 0x3ecf1a7bu, 0xbe007c2cu, 0xbe868fb1u}},
    {"spaghetti_3d_2", {0x3bf2e390u, 0xbd834d44u, 0x3e915344u, 0xbdb9c9bau}},
    {"spaghetti_3d_rarity", {0xbdd3f3b1u, 0xbe3678c0u, 0x3de1f1ffu, 0xbdf00a7au}},
    {"spaghetti_3d_thickness", {0xbd630c56u, 0xbeab0a50u, 0xbdbf3c65u, 0x3dac2ab5u}},
    {"spaghetti_roughness", {0xbeefe38au, 0xbe783b1eu, 0xbe2f08f8u, 0xbd7e8e00u}},
    {"spaghetti_roughness_modulator", {0x3d93637cu, 0xbf7a91abu, 0x3f20f874u, 0x3d564906u}},
    {"sulfur_cave_gradient", {0xbd9464ecu, 0x3c88c165u, 0xbf1505d0u, 0x3da349acu}},
    {"surface", {0x3df36c66u, 0x3f08268fu, 0xbec2a7efu, 0x3ed0497cu}},
    {"surface_secondary", {0xbe00b95eu, 0x3d835c67u, 0x3c44cf42u, 0x3e6aaac0u}},
    {"surface_swamp", {0x3e8c6230u, 0xbc762e20u, 0xbe835612u, 0x3e127812u}},
    {"temperature", {0xbcca9ff2u, 0xbe957dfcu, 0x3f44325bu, 0xbdb982c1u}},
    {"temperature_large", {0xbf56290du, 0xbf7cfdbeu, 0xbe93af08u, 0xbf5b268cu}},
    {"vegetation", {0x3f2e24afu, 0xbe858b35u, 0xbeb4002au, 0x3f156e3cu}},
    {"vegetation_large", {0x3e320cb2u, 0x3e19cd89u, 0xbe89125bu, 0x3db51c06u}},
};
const NoiseVec kNoise_0[] = {
    {"aquifer_barrier", {0xbf0a3eedu, 0x3f21921eu, 0xbea9da7cu, 0x3ce711ccu}},
    {"aquifer_fluid_level_floodedness", {0xbef12151u, 0xbf416287u, 0x3f05b547u, 0xbf4ec358u}},
    {"aquifer_fluid_level_spread", {0xbd6a934eu, 0x3dda6aa0u, 0x3e2336dcu, 0xbe9ef0f3u}},
    {"aquifer_lava", {0xbe282d7au, 0x3e1bcc14u, 0x3eb08e4cu, 0xbe6bbfd4u}},
    {"badlands_pillar", {0x3ef414aeu, 0x3e8a6aa0u, 0xbf65447du, 0xbc2acd34u}},
    {"badlands_pillar_roof", {0xbdc4b270u, 0xbf1bff6au, 0xbeee6416u, 0xbe2b45fbu}},
    {"badlands_surface", {0xbdd18287u, 0xbe3ed290u, 0xbe999526u, 0x3d427984u}},
    {"calcite", {0xbe776e32u, 0xbebb1d81u, 0xbef4db86u, 0xbe05f4cfu}},
    {"cave_cheese", {0xbd0efc69u, 0x3e7f660bu, 0xbe501347u, 0xbdde009fu}},
    {"cave_entrance", {0x3d1be31cu, 0xbe480718u, 0x3e0eea58u, 0x3e4d6c35u}},
    {"cave_layer", {0xbee2e2e5u, 0x3e3feaceu, 0xbdf5afa0u, 0xbec3a09fu}},
    {"clay_bands_offset", {0x3dd5a344u, 0x3ea6e85eu, 0x3c52e891u, 0xbc16c9b0u}},
    {"continentalness", {0xbcddb989u, 0x3dc8d58au, 0xbdad5abcu, 0xbb44af7au}},
    {"continentalness_large", {0xbf20792du, 0xbe6eb313u, 0xbe359524u, 0xbf191033u}},
    {"erosion", {0xbdd7da5eu, 0x3e8c0624u, 0xbe98091bu, 0xbd9084b1u}},
    {"erosion_large", {0x3c54582du, 0xbca22f29u, 0xbdb51e1au, 0x3d6968e5u}},
    {"gravel", {0xbe19c182u, 0x3db6cc2cu, 0xbe4adb55u, 0xbeeda5d5u}},
    {"gravel_layer", {0x3e310b1eu, 0xbef1ae67u, 0xbd62ce8du, 0x3edef6dcu}},
    {"ice", {0xbd85e710u, 0x3ebd6c05u, 0x3de37236u, 0x3db3308bu}},
    {"iceberg_pillar", {0x3ef3c3fau, 0xbf1848c2u, 0x3e8781e4u, 0xbe313567u}},
    {"iceberg_pillar_roof", {0xbd23c5b8u, 0xbed2e2d3u, 0xbe447546u, 0x3e5b0cd7u}},
    {"iceberg_surface", {0x3eea6e94u, 0xbf433227u, 0x3ea42a39u, 0xbe49184eu}},
    {"jagged", {0xbe477a99u, 0xbe51dde1u, 0xbe967bfau, 0xbe490232u}},
    {"nether_state_selector", {0xbedab651u, 0x3d3f3c58u, 0x3e280918u, 0x3dd55e56u}},
    {"nether_wart", {0xbdfdd1e2u, 0x3daa4700u, 0xbd5ff764u, 0xbf4c97f1u}},
    {"netherrack", {0xbe30b68cu, 0x3df6f902u, 0xbd34db6eu, 0xbe24d852u}},
    {"noodle", {0xbee9d666u, 0x3d022c10u, 0x3ee64912u, 0xbf029c76u}},
    {"noodle_ridge_a", {0xbe6e7de4u, 0xbe4b36cau, 0x3e6eab04u, 0xbdce81ccu}},
    {"noodle_ridge_b", {0xbe86a3f9u, 0xbe40c59cu, 0x3e8a1cb1u, 0xbe1d22b6u}},
    {"noodle_thickness", {0xbf0606c6u, 0xbe166fa0u, 0xbcd05f8au, 0xbf35c83au}},
    {"offset", {0xbf0b2876u, 0x3d688edeu, 0x3e25f046u, 0x3e095ad4u}},
    {"ore_gap", {0x3ee9cda0u, 0x3e415635u, 0xbe86cf52u, 0xbda23d38u}},
    {"ore_vein_a", {0xbf26d1a4u, 0xbd00296au, 0x3e719e21u, 0xbedb4f0cu}},
    {"ore_vein_b", {0xbec5e9c1u, 0x3ef038b7u, 0x3e5ea651u, 0xbe678703u}},
    {"ore_veininess", {0xbd880f08u, 0x3eb201fcu, 0x3f1c4764u, 0xbdae71ccu}},
    {"packed_ice", {0xbe2002bdu, 0x3e6345ccu, 0x3d5c0ee0u, 0x3e33357cu}},
    {"patch", {0x3d8b00a1u, 0xbe6494a6u, 0xbe1494ddu, 0xbe1a9df0u}},
    {"pillar", {0xbeb6140cu, 0x3e1ec0d8u, 0xbee4ea14u, 0xbc97c8d4u}},
    {"pillar_rareness", {0xbef1e35au, 0xbea8150eu, 0xbf3158eau, 0xbeca7ef9u}},
    {"pillar_thickness", {0xbe429265u, 0xbebd7b14u, 0xbf0c2b7fu, 0xbd921df0u}},
    {"powder_snow", {0xbda08cd4u, 0xbe19a15au, 0x3ed87d00u, 0xbd848486u}},
    {"ridge", {0x3cbdfd0fu, 0x3e1e28b8u, 0x3ddc0c76u, 0x3e6d5a58u}},
    {"small_patch", {0xbf15dd58u, 0x40097ccfu, 0x3f1d30adu, 0x3db146e8u}},
    {"soul_sand_layer", {0xbebebf2bu, 0x3cc44f43u, 0xbe78e6f1u, 0xbeb26a6fu}},
    {"spaghetti_2d", {0x3f2cdbbfu, 0x3e520e36u, 0xbcd727d8u, 0x3eb60b64u}},
    {"spaghetti_2d_elevation", {0xbdb01e50u, 0xbea2b3c2u, 0x3dc5e0a8u, 0xbe5e14d5u}},
    {"spaghetti_2d_modulator", {0x3e1eea7cu, 0x3e8b8d3fu, 0x3e5344c0u, 0x3e1a37d8u}},
    {"spaghetti_2d_thickness", {0xbf07545au, 0xbec079bau, 0x3e8a1330u, 0xbf0a78feu}},
    {"spaghetti_3d_1", {0xbe2b93b2u, 0xbeabbfa8u, 0xbdddcb98u, 0xbee3a481u}},
    {"spaghetti_3d_2", {0x3f0fd730u, 0x3e222146u, 0xbd8d0b3au, 0x3eaa15bcu}},
    {"spaghetti_3d_rarity", {0xbe01ad33u, 0xbc88a604u, 0x3de059c0u, 0xbde2fe26u}},
    {"spaghetti_3d_thickness", {0x3f25db38u, 0xbe223fabu, 0x3efe3f96u, 0x3f17bfd9u}},
    {"spaghetti_roughness", {0x3dbb3d00u, 0x3e30baceu, 0x3e40e7e0u, 0xbeba6e5cu}},
    {"spaghetti_roughness_modulator", {0x3e05e0acu, 0x3e376c4du, 0xbd1788d3u, 0xbd7959e0u}},
    {"sulfur_cave_gradient", {0x3e44e6cfu, 0x3c167588u, 0xbe790426u, 0x3bc38b70u}},
    {"surface", {0x3f49c66du, 0xbede5deau, 0xbf2ce84bu, 0xbe69ca82u}},
    {"surface_secondary", {0xbdb9b76cu, 0x3e11889fu, 0x3e9ab2afu, 0xbe84a4acu}},
    {"surface_swamp", {0xbf02c662u, 0x3ead5e70u, 0x3dfd9ff2u, 0x3f25f6d6u}},
    {"temperature", {0x3dfa8994u, 0x3e8b3f11u, 0x3cf7d8aeu, 0x3e299c66u}},
    {"temperature_large", {0x3e78b31bu, 0x3e92de84u, 0x3ea4f460u, 0x3e70ac21u}},
    {"vegetation", {0xbc132888u, 0xbe8e3c4au, 0xbea5d004u, 0xbd40ebacu}},
    {"vegetation_large", {0xbe2c1e79u, 0xbc81c2c6u, 0x3ef55b22u, 0xbe1e9589u}},
};
const NoiseVec kNoise_m1[] = {
    {"aquifer_barrier", {0xbd6bf0d8u, 0xbd8a9bc5u, 0x3e838171u, 0x3db5acd6u}},
    {"aquifer_fluid_level_floodedness", {0x3d808633u, 0x3e2f6b79u, 0xbd5b1f80u, 0xbe62d08bu}},
    {"aquifer_fluid_level_spread", {0x3ee80808u, 0x3ddf0058u, 0x3e615473u, 0xbec0191cu}},
    {"aquifer_lava", {0xbd5edbb1u, 0x3e21b902u, 0xbe618a51u, 0x3e8557f4u}},
    {"badlands_pillar", {0xbc4bffdcu, 0x3d141363u, 0x3f091162u, 0x3e4b67aau}},
    {"badlands_pillar_roof", {0x3e4b384eu, 0xbe7fd6c3u, 0x3d8b692bu, 0x3e05a712u}},
    {"badlands_surface", {0xbf0ebb44u, 0xbef307f7u, 0x3e48bd5au, 0xbd9198f8u}},
    {"calcite", {0xbc1805c2u, 0x3e2b5504u, 0x3ed3034fu, 0x3ceb80b9u}},
    {"cave_cheese", {0x3ed15108u, 0x3e98404cu, 0xbdbf772eu, 0x3d882768u}},
    {"cave_entrance", {0x3d7aeeecu, 0x3e80e68du, 0xbd8795c2u, 0x3d6e3372u}},
    {"cave_layer", {0xbc5128f0u, 0x3e9708f6u, 0x3e0257deu, 0xbd4a95deu}},
    {"clay_bands_offset", {0xbec836e4u, 0x3d2e238au, 0x3e3d47a5u, 0xbe7cb779u}},
    {"continentalness", {0xbf1c9ff9u, 0xbf2f7eceu, 0xbcde65b9u, 0xbf3340f7u}},
    {"continentalness_large", {0x3e243987u, 0x3e354912u, 0x3e2d73c5u, 0x3e1e9bf9u}},
    {"erosion", {0x3e81135au, 0x3ec693cdu, 0xbdc91c16u, 0x3e76fab3u}},
    {"erosion_large", {0x3e16a1dfu, 0x3e1fabd8u, 0x3e36928au, 0x3e5d9cecu}},
    {"gravel", {0x3e1d0dfcu, 0x3e3ee36eu, 0x3e995f1cu, 0x3db872a6u}},
    {"gravel_layer", {0xbda90f88u, 0xbf347ad2u, 0xbc30ae04u, 0x3d97867au}},
    {"ice", {0x3e8bae94u, 0x3ebd3c7du, 0xbe98a5f7u, 0x3ec3f5a5u}},
    {"iceberg_pillar", {0xbdf6db66u, 0xbe9d3516u, 0xbea5766au, 0xbeb3a3d6u}},
    {"iceberg_pillar_roof", {0xbd9a2004u, 0x3e662604u, 0xbe5585f7u, 0xbe5cfc5fu}},
    {"iceberg_surface", {0x3ddbc0b8u, 0x3f0ebae2u, 0x3eae9472u, 0x3de4f35bu}},
    {"jagged", {0x3ed74ed1u, 0x3edd90eeu, 0x3e951ba0u, 0x3ed6e547u}},
    {"nether_state_selector", {0x3dad9a99u, 0x3f362158u, 0xbec9f227u, 0x3f00ac99u}},
    {"nether_wart", {0xbee7c8dbu, 0x3ea7489du, 0x3d9a1e7cu, 0xbe3348cdu}},
    {"netherrack", {0x3edf2aedu, 0xbe0ed381u, 0xbe27ffa2u, 0x3ed17ca7u}},
    {"noodle", {0x3ec39b05u, 0x3e5c3f17u, 0xbe212a38u, 0x3e8b1786u}},
    {"noodle_ridge_a", {0x3ebcb46au, 0x3efa6d25u, 0xbe8e4dcbu, 0xbd2de127u}},
    {"noodle_ridge_b", {0x3ee4ea34u, 0xbe9d0803u, 0x3e7420a2u, 0x3ea10306u}},
    {"noodle_thickness", {0x3d636100u, 0xbd5f16b1u, 0xbe01cdf2u, 0x3b1c9500u}},
    {"offset", {0x3e96c284u, 0x3e9533acu, 0xbe4d99a2u, 0x3d1c9beau}},
    {"ore_gap", {0x3d42a218u, 0x3e2d1e36u, 0x3e96c566u, 0x3d7fcf54u}},
    {"ore_vein_a", {0x3cfdd968u, 0xbf5157ffu, 0xbe129539u, 0x3d94bed4u}},
    {"ore_vein_b", {0xbe24462du, 0xbe910bd8u, 0xbe7f1a7eu, 0xbde83438u}},
    {"ore_veininess", {0xbe58b4a6u, 0xbed85618u, 0x3e1ad61cu, 0x3d1f2479u}},
    {"packed_ice", {0xbdf57ba7u, 0x3dceba6bu, 0xbe8a921fu, 0x3d596dffu}},
    {"patch", {0xbe93aff7u, 0xbddc77f5u, 0xbee281d4u, 0xba208ae8u}},
    {"pillar", {0xbe4d08d7u, 0x3e6f3174u, 0x3dd33742u, 0xbe3d1af6u}},
    {"pillar_rareness", {0x3e826adbu, 0x3e9c233cu, 0xbe83f222u, 0x3e623bc1u}},
    {"pillar_thickness", {0x3eb95b1bu, 0xbee7150du, 0xbe34d8f7u, 0x3e8c37afu}},
    {"powder_snow", {0x3e0d1908u, 0xbe4a259cu, 0x3d2194f1u, 0xbf02d37eu}},
    {"ridge", {0xbd8aa91eu, 0x3e898de5u, 0xbec82f90u, 0xbea43ab6u}},
    {"small_patch", {0x3e8e358cu, 0x3f8644e0u, 0x3e18df26u, 0xbf16fcc6u}},
    {"soul_sand_layer", {0x3eeb2d5du, 0xbf014a83u, 0x3ecfba9eu, 0x3dd3e3ecu}},
    {"spaghetti_2d", {0xbe0e91b6u, 0x3e68d558u, 0xbdc6e348u, 0xbe1cd2eeu}},
    {"spaghetti_2d_elevation", {0xbdc7165eu, 0xbe45a40cu, 0x3ed33a1fu, 0x3e07f8b4u}},
    {"spaghetti_2d_modulator", {0x3d496447u, 0x3df55bdbu, 0xbf756546u, 0x3d9683d9u}},
    {"spaghetti_2d_thickness", {0x3e70a210u, 0x3ea523cau, 0x3e2f4131u, 0x3e64d22du}},
    {"spaghetti_3d_1", {0xbebcea11u, 0x3ec8e4d8u, 0x3dd9580eu, 0x3b59ea40u}},
    {"spaghetti_3d_2", {0xbdd436a3u, 0xbe6e0b42u, 0x3e610ee8u, 0x3c4833c0u}},
    {"spaghetti_3d_rarity", {0x3c513654u, 0x3d8d0ff9u, 0xbed509c4u, 0x3c20eab0u}},
    {"spaghetti_3d_thickness", {0xbdfa6197u, 0xbd984b72u, 0x3c4d0960u, 0xbd7b8a00u}},
    {"spaghetti_roughness", {0x3ebd404eu, 0xbe84377bu, 0xbf785c70u, 0x3e737ea0u}},
    {"spaghetti_roughness_modulator", {0x3e18604eu, 0xbf6f5cccu, 0xbebdd73bu, 0x3d4bf85au}},
    {"sulfur_cave_gradient", {0x3c5f5c28u, 0xbd6cbe0du, 0xbc0292f8u, 0xbe7fbf58u}},
    {"surface", {0xbd6fe394u, 0xbf0ee425u, 0xbdcf76d0u, 0x3eb63238u}},
    {"surface_secondary", {0x3d240d90u, 0xbc282924u, 0x3dea6158u, 0x3e3bf0b6u}},
    {"surface_swamp", {0xbf109e5bu, 0xbe16369du, 0xbc290da0u, 0x3e3c247au}},
    {"temperature", {0x3e250e45u, 0x3d33630au, 0x3f09e480u, 0x3d48ccdeu}},
    {"temperature_large", {0x3ec52f1cu, 0x3ed0cbfcu, 0x3e656f5du, 0x3ece8975u}},
    {"vegetation", {0xbe105d07u, 0xbe4979d1u, 0x3ed5e115u, 0xbdbafe6bu}},
    {"vegetation_large", {0xbdd3232fu, 0xbe84a486u, 0x3e7b15cdu, 0xbe001ffau}},
};
const NoiseVec kNoise_8675309123456789[] = {
    {"aquifer_barrier", {0x3dc8647du, 0x3d5dfcb2u, 0x3e6ba5dau, 0x3e5a149eu}},
    {"aquifer_fluid_level_floodedness", {0x3dfc7a9au, 0xbddb931cu, 0xbf4e5842u, 0x3e1b719du}},
    {"aquifer_fluid_level_spread", {0xbe281c5fu, 0x3e23b01bu, 0x3d423a60u, 0x3e35b480u}},
    {"aquifer_lava", {0x3ee24543u, 0x3c8a1d08u, 0xbd5cbc4cu, 0x3e2fc0eeu}},
    {"badlands_pillar", {0x3e5106a8u, 0x3e7ec2b7u, 0xbe38624bu, 0xbe3055acu}},
    {"badlands_pillar_roof", {0x3dd28ebbu, 0xbd71a7c0u, 0x3dba9b2au, 0x3e3c039fu}},
    {"badlands_surface", {0xbe8dae7bu, 0xbed7c655u, 0x3d588160u, 0xbb86b006u}},
    {"calcite", {0xbdefefefu, 0xbdc422c1u, 0x3c191949u, 0xbd5e1eb4u}},
    {"cave_cheese", {0x3e2ee662u, 0x3afba57cu, 0xbeeaa24bu, 0x3f12ec36u}},
    {"cave_entrance", {0xbd71a156u, 0xbdfe9fe4u, 0xbe045adau, 0xbdc2e0e3u}},
    {"cave_layer", {0x3eb3a4d1u, 0xbb10a180u, 0xbf174ffcu, 0x3de03e01u}},
    {"clay_bands_offset", {0xbee71e50u, 0x3f0038fau, 0x3dfff9a2u, 0xbe4d9e44u}},
    {"continentalness", {0x3c920081u, 0xbe1200bau, 0xbe5da6e3u, 0x3e9eebf1u}},
    {"continentalness_large", {0xbf1d4cafu, 0xbe6ced63u, 0x3d5e3560u, 0xbf14805bu}},
    {"erosion", {0xbcb74470u, 0x3d8bf0b8u, 0x3ebd2040u, 0xbd94f28fu}},
    {"erosion_large", {0xbdd4ec44u, 0xbcd825a2u, 0x3ee82212u, 0xbdb307e5u}},
    {"gravel", {0x3d5a51e9u, 0x3ee9e1bfu, 0x3dd83798u, 0x3e9c5e8eu}},
    {"gravel_layer", {0x3f0598c8u, 0xbec54558u, 0x3d590d77u, 0x3e4be218u}},
    {"ice", {0xbe92928eu, 0x3ed8b68bu, 0x3edbaa93u, 0x3dbc3e5bu}},
    {"iceberg_pillar", {0x3d18af0au, 0xbe5b369du, 0x3e4fc7d2u, 0x3dfbbb84u}},
    {"iceberg_pillar_roof", {0xbdeeb9c0u, 0x3ec1126cu, 0xbe0a26f0u, 0xbed6a6c2u}},
    {"iceberg_surface", {0xbe3d49c8u, 0xbe7987d6u, 0xbe22b022u, 0xbd2e7659u}},
    {"jagged", {0xbe73065du, 0xbe74faebu, 0xbe1771f3u, 0xbe735755u}},
    {"nether_state_selector", {0x3e462272u, 0x3ed50ea4u, 0xbe63a5eau, 0xbe6443e7u}},
    {"nether_wart", {0xbe97fe6du, 0xbf079170u, 0xbe3ed640u, 0x3ec08ef0u}},
    {"netherrack", {0x3ec4df88u, 0x3ea974e1u, 0x3ed21d1bu, 0x3e690048u}},
    {"noodle", {0xbe062a8bu, 0xbe8685deu, 0xbf020b9du, 0xbe3bc510u}},
    {"noodle_ridge_a", {0xbf032c66u, 0x3e95b2b0u, 0x3e9bd1d4u, 0xbf11dff4u}},
    {"noodle_ridge_b", {0x3de7d190u, 0xbe6e710eu, 0x3ee019a5u, 0x3de42cb0u}},
    {"noodle_thickness", {0x3ea6117cu, 0x3ebb2762u, 0xbe23733cu, 0x3e8f6f80u}},
    {"offset", {0x3e872febu, 0xbe366284u, 0x3e46ba84u, 0xbd56fc50u}},
    {"ore_gap", {0xbf2a2152u, 0xbf61072eu, 0x3ddb87eau, 0x3dac3da5u}},
    {"ore_vein_a", {0x3ebbe91cu, 0x3d9c44f6u, 0x3e882bdfu, 0xbdf1c87fu}},
    {"ore_vein_b", {0x3e284f67u, 0xbeab505bu, 0xbdf9a9e6u, 0x3e581ba4u}},
    {"ore_veininess", {0x3e7984b6u, 0xbe5402c6u, 0xbe14a5adu, 0x3df1b3d4u}},
    {"packed_ice", {0xbd62ee6bu, 0x3d83f666u, 0x3e0f17a6u, 0x3e7b58b6u}},
    {"patch", {0x3e8c2090u, 0xbe972d55u, 0x3eddaa2au, 0x3ea47bc4u}},
    {"pillar", {0xbf02b461u, 0xbeb476a8u, 0x3e216a87u, 0xbeb09899u}},
    {"pillar_rareness", {0xbe0cf3fcu, 0xbe33dc34u, 0xbe6b9fdeu, 0xbcf23410u}},
    {"pillar_thickness", {0xbe9848ccu, 0xbd660ad4u, 0x3e0e122cu, 0xbe4da212u}},
    {"powder_snow", {0xbdfd5238u, 0xbe1a0dfbu, 0x3e044053u, 0xbd1e00aau}},
    {"ridge", {0x3f3c8a4eu, 0x3dcdd5e4u, 0x3cfcfe9eu, 0x3ea924fdu}},
    {"small_patch", {0xbef30f7cu, 0xbf9fec9bu, 0x3f1fbcfeu, 0xbf85dfd6u}},
    {"soul_sand_layer", {0x3e2b238du, 0xbe30c8abu, 0xbd74aea4u, 0x3e986c49u}},
    {"spaghetti_2d", {0x3e9a77dbu, 0x3e3fe584u, 0xbd82ec4du, 0x3d71a4c8u}},
    {"spaghetti_2d_elevation", {0xbd8a0944u, 0x3e700dd3u, 0x3e310568u, 0xbda34082u}},
    {"spaghetti_2d_modulator", {0x3ed7eaa8u, 0x3f034dc0u, 0xbf25b756u, 0x3edf361fu}},
    {"spaghetti_2d_thickness", {0x3f142bbeu, 0x3f170402u, 0x3de3d4cfu, 0x3f162450u}},
    {"spaghetti_3d_1", {0x3f07007au, 0xbd1e7260u, 0xbe92c00eu, 0x3ea385d2u}},
    {"spaghetti_3d_2", {0xbe2b0755u, 0xbef3730au, 0xbec2fe86u, 0xbec92e6bu}},
    {"spaghetti_3d_rarity", {0xbd0dd61cu, 0xbd774aa8u, 0x3e13e36du, 0xbcbee2c8u}},
    {"spaghetti_3d_thickness", {0x3dc74f85u, 0x3e3e62a1u, 0xbe8de4f7u, 0x3df24f91u}},
    {"spaghetti_roughness", {0x3d970bd0u, 0xbf075aebu, 0x3e2565c8u, 0xbec23de8u}},
    {"spaghetti_roughness_modulator", {0x3d887eb6u, 0xbdce9d0cu, 0xbf5a0da9u, 0xbe8788fcu}},
    {"sulfur_cave_gradient", {0x3f09c448u, 0xbd1985b1u, 0x3f3725d7u, 0xbd1f40b5u}},
    {"surface", {0x3cb9772eu, 0x3d40ed5fu, 0xbe8b185fu, 0xbdd13f67u}},
    {"surface_secondary", {0xbdcd7657u, 0xbd394dcbu, 0x3ec8bdf5u, 0xbd898fb1u}},
    {"surface_swamp", {0xbe4788cfu, 0x3e7dc1ddu, 0xbee38b5du, 0x3e9d9d62u}},
    {"temperature", {0x3e4f3c55u, 0x3f0cb641u, 0x3e7f392eu, 0x3e7bb46eu}},
    {"temperature_large", {0x3e3e9bbeu, 0x3e1ebee1u, 0xbed66943u, 0x3e4d0b0fu}},
    {"vegetation", {0x3e4f7357u, 0x3e0955d6u, 0xbe3c1617u, 0x3dd9ec24u}},
    {"vegetation_large", {0x3dbe6ccau, 0xbe9c8178u, 0xbed73e57u, 0x3dc5f2e8u}},
};

// Derived parameters of every noise file [REAL: noise-derived-table.txt]: normalizationFactor, per octave
// (index, frequency, amplitude, float amplitude).
const DerivedVec kDerived[] = {
    {"aquifer_barrier", 0x3febe96eb0ed53aeull, {
        {-3, 0x3fc0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"aquifer_fluid_level_floodedness", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"aquifer_fluid_level_spread", 0x3febe96eb0ed53aeull, {
        {-5, 0x3fa0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"aquifer_lava", 0x3febe96eb0ed53aeull, {
        {-1, 0x3fe0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"badlands_pillar", 0x3ff6b4bb713aca51ull, {
        {-2, 0x3fd0000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-1, 0x3fe0000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {0, 0x3ff0000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {1, 0x4000000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"badlands_pillar_roof", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"badlands_surface", 0x3ff5516a04816e8dull, {
        {-6, 0x3f90000000000000ull, 0x3fe127b919be8d44ull, 0x3f36db6eu},
        {-5, 0x3fa0000000000000ull, 0x3fd127b919be8d44ull, 0x3eb6db6eu},
        {-4, 0x3fb0000000000000ull, 0x3fc127b919be8d44ull, 0x3e36db6eu},
    }},
    {"calcite", 0x3ff6b4bb713aca51ull, {
        {-9, 0x3f60000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-8, 0x3f70000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {-7, 0x3f80000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {-6, 0x3f90000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"cave_cheese", 0x3ffc596be1d802b7ull, {
        {-8, 0x3f70000000000000ull, 0x3fcacefb4d73f6fbull, 0x3ebe0030u},
        {-7, 0x3f80000000000000ull, 0x3fcacefb4d73f6fbull, 0x3ebe0030u},
        {-6, 0x3f90000000000000ull, 0x3fcacefb4d73f6fbull, 0x3ebe0030u},
        {-5, 0x3fa0000000000000ull, 0x3faacefb4d73f6fbull, 0x3dbe0030u},
        {-4, 0x3fb0000000000000ull, 0x3faacefb4d73f6fbull, 0x3dbe0030u},
        {-3, 0x3fc0000000000000ull, 0x3f8acefb4d73f6fbull, 0x3cbe0030u},
        {-1, 0x3fe0000000000000ull, 0x3f7acefb4d73f6fbull, 0x3c3e0030u},
    }},
    {"cave_entrance", 0x3ff787145c3ad350ull, {
        {-7, 0x3f80000000000000ull, 0x3fc8ded46e1ddd1bull, 0x3e924925u},
        {-6, 0x3f90000000000000ull, 0x3fbf168989a55461ull, 0x3e36db6eu},
        {-5, 0x3fa0000000000000ull, 0x3fbf168989a55461ull, 0x3e36db6eu},
    }},
    {"cave_layer", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"clay_bands_offset", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"continentalness", 0x3ffb064520309512ull, {
        {-9, 0x3f60000000000000ull, 0x3fdc796a5aced1d1ull, 0x3f406030u},
        {-8, 0x3f70000000000000ull, 0x3fcc796a5aced1d1ull, 0x3ec06030u},
        {-7, 0x3f80000000000000ull, 0x3fcc796a5aced1d1ull, 0x3ec06030u},
        {-6, 0x3f90000000000000ull, 0x3fbc796a5aced1d1ull, 0x3e406030u},
        {-5, 0x3fa0000000000000ull, 0x3fac796a5aced1d1ull, 0x3dc06030u},
        {-4, 0x3fb0000000000000ull, 0x3f8c796a5aced1d1ull, 0x3cc06030u},
        {-3, 0x3fc0000000000000ull, 0x3f7c796a5aced1d1ull, 0x3c406030u},
        {-2, 0x3fd0000000000000ull, 0x3f6c796a5aced1d1ull, 0x3bc06030u},
        {-1, 0x3fe0000000000000ull, 0x3f5c796a5aced1d1ull, 0x3b406030u},
    }},
    {"continentalness_large", 0x3ffb064520309512ull, {
        {-11, 0x3f40000000000000ull, 0x3fdc796a5aced1d1ull, 0x3f406030u},
        {-10, 0x3f50000000000000ull, 0x3fcc796a5aced1d1ull, 0x3ec06030u},
        {-9, 0x3f60000000000000ull, 0x3fcc796a5aced1d1ull, 0x3ec06030u},
        {-8, 0x3f70000000000000ull, 0x3fbc796a5aced1d1ull, 0x3e406030u},
        {-7, 0x3f80000000000000ull, 0x3fac796a5aced1d1ull, 0x3dc06030u},
        {-6, 0x3f90000000000000ull, 0x3f8c796a5aced1d1ull, 0x3cc06030u},
        {-5, 0x3fa0000000000000ull, 0x3f7c796a5aced1d1ull, 0x3c406030u},
        {-4, 0x3fb0000000000000ull, 0x3f6c796a5aced1d1ull, 0x3bc06030u},
        {-3, 0x3fc0000000000000ull, 0x3f5c796a5aced1d1ull, 0x3b406030u},
    }},
    {"erosion", 0x3ff4e6d2bdd08b4eull, {
        {-9, 0x3f60000000000000ull, 0x3fe18f4343d3c75aull, 0x3f378336u},
        {-8, 0x3f70000000000000ull, 0x3fd18f4343d3c75aull, 0x3eb78336u},
        {-6, 0x3f90000000000000ull, 0x3fb18f4343d3c75aull, 0x3db78336u},
        {-5, 0x3fa0000000000000ull, 0x3fa18f4343d3c75aull, 0x3d378336u},
    }},
    {"erosion_large", 0x3ff4e6d2bdd08b4eull, {
        {-11, 0x3f40000000000000ull, 0x3fe18f4343d3c75aull, 0x3f378336u},
        {-10, 0x3f50000000000000ull, 0x3fd18f4343d3c75aull, 0x3eb78336u},
        {-8, 0x3f70000000000000ull, 0x3fb18f4343d3c75aull, 0x3db78336u},
        {-7, 0x3f80000000000000ull, 0x3fa18f4343d3c75aull, 0x3d378336u},
    }},
    {"gravel", 0x3ff6b4bb713aca51ull, {
        {-8, 0x3f70000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-7, 0x3f80000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {-6, 0x3f90000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {-5, 0x3fa0000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"gravel_layer", 0x3ff6b4e4c698bd40ull, {
        {-8, 0x3f70000000000000ull, 0x3fe0f1c87cef6c04ull, 0x3f406030u},
        {-7, 0x3f80000000000000ull, 0x3fd0f1c87cef6c04ull, 0x3ec06030u},
        {-6, 0x3f90000000000000ull, 0x3fc0f1c87cef6c04ull, 0x3e406030u},
        {-5, 0x3fa0000000000000ull, 0x3fb0f1c87cef6c04ull, 0x3dc06030u},
        {0, 0x3ff0000000000000ull, 0x3efceb26612b6304ull, 0x3824291fu},
    }},
    {"ice", 0x3ff6b4bb713aca51ull, {
        {-4, 0x3fb0000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-3, 0x3fc0000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {-2, 0x3fd0000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {-1, 0x3fe0000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"iceberg_pillar", 0x3ff6b4bb713aca51ull, {
        {-6, 0x3f90000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-5, 0x3fa0000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {-4, 0x3fb0000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {-3, 0x3fc0000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"iceberg_pillar_roof", 0x3febe96eb0ed53aeull, {
        {-3, 0x3fc0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"iceberg_surface", 0x3ff5516a04816e8dull, {
        {-6, 0x3f90000000000000ull, 0x3fe127b919be8d44ull, 0x3f36db6eu},
        {-5, 0x3fa0000000000000ull, 0x3fd127b919be8d44ull, 0x3eb6db6eu},
        {-4, 0x3fb0000000000000ull, 0x3fc127b919be8d44ull, 0x3e36db6eu},
    }},
    {"jagged", 0x3ff82c081361c4cfull, {
        {-16, 0x3ef0000000000000ull, 0x3fe09cfc11a88280ull, 0x3f48c992u},
        {-15, 0x3f00000000000000ull, 0x3fd09cfc11a88280ull, 0x3ec8c992u},
        {-14, 0x3f10000000000000ull, 0x3fc09cfc11a88280ull, 0x3e48c992u},
        {-13, 0x3f20000000000000ull, 0x3fb09cfc11a88280ull, 0x3dc8c992u},
        {-12, 0x3f30000000000000ull, 0x3fa09cfc11a88280ull, 0x3d48c992u},
        {-11, 0x3f40000000000000ull, 0x3f909cfc11a88280ull, 0x3cc8c992u},
        {-10, 0x3f50000000000000ull, 0x3f809cfc11a88280ull, 0x3c48c992u},
        {-9, 0x3f60000000000000ull, 0x3f709cfc11a88280ull, 0x3bc8c992u},
        {-8, 0x3f70000000000000ull, 0x3f609cfc11a88280ull, 0x3b48c992u},
        {-7, 0x3f80000000000000ull, 0x3f509cfc11a88280ull, 0x3ac8c992u},
        {-6, 0x3f90000000000000ull, 0x3f409cfc11a88280ull, 0x3a48c992u},
        {-5, 0x3fa0000000000000ull, 0x3f309cfc11a88280ull, 0x39c8c992u},
        {-4, 0x3fb0000000000000ull, 0x3f209cfc11a88280ull, 0x3948c992u},
        {-3, 0x3fc0000000000000ull, 0x3f109cfc11a88280ull, 0x38c8c992u},
        {-2, 0x3fd0000000000000ull, 0x3f009cfc11a88280ull, 0x3848c992u},
        {-1, 0x3fe0000000000000ull, 0x3ef09cfc11a88280ull, 0x37c8c992u},
    }},
    {"nether_state_selector", 0x3febe96eb0ed53aeull, {
        {-4, 0x3fb0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"nether_wart", 0x3feedb7640935fb8ull, {
        {-3, 0x3fc0000000000000ull, 0x3fe79927313cbe2full, 0x3f360b61u},
        {0, 0x3ff0000000000000ull, 0x3fb53d09ac5044c4ull, 0x3da3d70au},
    }},
    {"netherrack", 0x3fed1aeaee8429aaull, {
        {-3, 0x3fc0000000000000ull, 0x3fe904d465f0c0eaull, 0x3f360b61u},
        {0, 0x3ff0000000000000ull, 0x3fa183617a8eed70ull, 0x3cfedcbbu},
    }},
    {"noodle", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"noodle_ridge_a", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"noodle_ridge_b", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"noodle_thickness", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"offset", 0x3ff5516a04816e8dull, {
        {-3, 0x3fc0000000000000ull, 0x3fe002f106f61d72ull, 0x3f2aaaabu},
        {-2, 0x3fd0000000000000ull, 0x3fd002f106f61d72ull, 0x3eaaaaabu},
        {-1, 0x3fe0000000000000ull, 0x3fc002f106f61d72ull, 0x3e2aaaabu},
    }},
    {"ore_gap", 0x3febe96eb0ed53aeull, {
        {-5, 0x3fa0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"ore_vein_a", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"ore_vein_b", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"ore_veininess", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"packed_ice", 0x3ff6b4bb713aca51ull, {
        {-7, 0x3f80000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-6, 0x3f90000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {-5, 0x3fa0000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {-4, 0x3fb0000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"patch", 0x3febec68b63370e7ull, {
        {-5, 0x3fa0000000000000ull, 0x3fea9c1d1481e4dbull, 0x3f39c278u},
        {0, 0x3ff0000000000000ull, 0x3f36b500ebf3f67eull, 0x399e83d0u},
    }},
    {"pillar", 0x3ff2b94d40666c6aull, {
        {-7, 0x3f80000000000000ull, 0x3fe441639d5be97cull, 0x3f3da12fu},
        {-6, 0x3f90000000000000ull, 0x3fd441639d5be97cull, 0x3ebda12fu},
    }},
    {"pillar_rareness", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"pillar_thickness", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"powder_snow", 0x3ff6b4bb713aca51ull, {
        {-6, 0x3f90000000000000ull, 0x3fe008f130fe0265ull, 0x3f360b61u},
        {-5, 0x3fa0000000000000ull, 0x3fd008f130fe0265ull, 0x3eb60b61u},
        {-4, 0x3fb0000000000000ull, 0x3fc008f130fe0265ull, 0x3e360b61u},
        {-3, 0x3fc0000000000000ull, 0x3fb008f130fe0265ull, 0x3db60b61u},
    }},
    {"ridge", 0x3ff5dd5edbee6c3dull, {
        {-7, 0x3f80000000000000ull, 0x3fddbc49ffb096e1ull, 0x3f228a29u},
        {-6, 0x3f90000000000000ull, 0x3fddbc49ffb096e1ull, 0x3f228a29u},
        {-5, 0x3fa0000000000000ull, 0x3fbdbc49ffb096e1ull, 0x3e228a29u},
    }},
    {"small_patch", 0x3febe96eb0ed53b0ull, {
        {-3, 0x3fc0000000000000ull, 0x4006ede8c7ae9c5eull, 0x40200000u},
    }},
    {"soul_sand_layer", 0x3ff6b4e4c698bd40ull, {
        {-8, 0x3f70000000000000ull, 0x3fe0f1c87cef6c04ull, 0x3f406030u},
        {-7, 0x3f80000000000000ull, 0x3fd0f1c87cef6c04ull, 0x3ec06030u},
        {-6, 0x3f90000000000000ull, 0x3fc0f1c87cef6c04ull, 0x3e406030u},
        {-5, 0x3fa0000000000000ull, 0x3fb0f1c87cef6c04ull, 0x3dc06030u},
        {0, 0x3ff0000000000000ull, 0x3efceb26612b6304ull, 0x3824291fu},
    }},
    {"spaghetti_2d", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_2d_elevation", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_2d_modulator", 0x3febe96eb0ed53aeull, {
        {-11, 0x3f40000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_2d_thickness", 0x3febe96eb0ed53aeull, {
        {-11, 0x3f40000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_3d_1", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_3d_2", 0x3febe96eb0ed53aeull, {
        {-7, 0x3f80000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_3d_rarity", 0x3febe96eb0ed53aeull, {
        {-11, 0x3f40000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_3d_thickness", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_roughness", 0x3febe96eb0ed53aeull, {
        {-5, 0x3fa0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"spaghetti_roughness_modulator", 0x3febe96eb0ed53aeull, {
        {-8, 0x3f70000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"sulfur_cave_gradient", 0x3ff0ec8e05c3aa98ull, {
        {-5, 0x3fa0000000000000ull, 0x3fe59bf1b99f3397ull, 0x3f36db6eu},
        {-3, 0x3fc0000000000000ull, 0x3fc59bf1b99f3397ull, 0x3e36db6eu},
    }},
    {"surface", 0x3ff5516a04816e8dull, {
        {-6, 0x3f90000000000000ull, 0x3fe127b919be8d44ull, 0x3f36db6eu},
        {-5, 0x3fa0000000000000ull, 0x3fd127b919be8d44ull, 0x3eb6db6eu},
        {-4, 0x3fb0000000000000ull, 0x3fc127b919be8d44ull, 0x3e36db6eu},
    }},
    {"surface_secondary", 0x3ff428970e00bc6full, {
        {-6, 0x3f90000000000000ull, 0x3fe20faeb1c49dd5ull, 0x3f360b61u},
        {-5, 0x3fa0000000000000ull, 0x3fd20faeb1c49dd5ull, 0x3eb60b61u},
        {-3, 0x3fc0000000000000ull, 0x3fb20faeb1c49dd5ull, 0x3db60b61u},
    }},
    {"surface_swamp", 0x3febe96eb0ed53aeull, {
        {-2, 0x3fd0000000000000ull, 0x3fee928bb4e8d07eull, 0x3f555555u},
    }},
    {"temperature", 0x3ff00f74db28a272ull, {
        {-10, 0x3f50000000000000ull, 0x3fee5c93158b2016ull, 0x3f73cf3du},
        {-8, 0x3f70000000000000ull, 0x3fc43db763b21564ull, 0x3e228a29u},
    }},
    {"temperature_large", 0x3ff00f74db28a272ull, {
        {-12, 0x3f30000000000000ull, 0x3fee5c93158b2016ull, 0x3f73cf3du},
        {-10, 0x3f50000000000000ull, 0x3fc43db763b21564ull, 0x3e228a29u},
    }},
    {"vegetation", 0x3ff2b94d40666c6aull, {
        {-8, 0x3f70000000000000ull, 0x3fdedd8b9a73acebull, 0x3f107acfu},
        {-7, 0x3f80000000000000ull, 0x3fcedd8b9a73acebull, 0x3e907acfu},
    }},
    {"vegetation_large", 0x3ff2b94d40666c6aull, {
        {-10, 0x3f50000000000000ull, 0x3fdedd8b9a73acebull, 0x3f107acfu},
        {-9, 0x3f60000000000000ull, 0x3fcedd8b9a73acebull, 0x3e907acfu},
    }},
};

struct SeedNoise {
    std::int64_t seed;
    const NoiseVec* vecs;
    std::size_t count;
};

#define SEED_NOISE(seed, table) SeedNoise{seed, table, sizeof(table) / sizeof(table[0])}

void testNormalNoise(const std::filesystem::path& worldgen) {
    const struct {
        double x, y, z;
    } points[4] = {{0.0, 0.0, 0.0}, {100.5, 0.0, -250.25}, {-1234.567, 0.0, 8910.11}, {37.0, -20.0, -3.0}};
    const SeedNoise seeds[] = {SEED_NOISE(12345, kNoise_12345), SEED_NOISE(0, kNoise_0), SEED_NOISE(-1, kNoise_m1),
                               SEED_NOISE(8675309123456789ll, kNoise_8675309123456789)};
    for (const SeedNoise& sn : seeds) {
        const RandomState rs(sn.seed, worldgen);
        for (std::size_t i = 0; i < sn.count; ++i) {
            std::string err;
            const NormalNoise* n = rs.noise(sn.vecs[i].name, err);
            if (!n) {
                std::fprintf(stderr, "noise %s: %s\n", sn.vecs[i].name, err.c_str());
                CHECK(n != nullptr);
                continue;
            }
            for (int p = 0; p < 4; ++p) {
                checkBits(std::string("noise seed ") + std::to_string(sn.seed) + " " + sn.vecs[i].name + " P" + std::to_string(p + 1),
                          bitsOf(n->get(points[p].x, points[p].y, points[p].z)), sn.vecs[i].bits[p]);
            }
        }
    }

    // All 62 noise files exist and instantiate; the derived parameters match the real objects.
    const RandomState rs(12345, worldgen);
    // 62 overworld/cave/surface noises + noise/nether/temperature and vegetation (the game builds those two differently)
    CHECK(rs.noiseKeys().size() == 64);
    std::string err;
    CHECK(rs.createAllNoises(err));
    if (!err.empty()) std::fprintf(stderr, "createAllNoises: %s\n", err.c_str());
    for (const DerivedVec& dv : kDerived) {
        const NormalNoise* n = rs.noise(dv.name, err);
        CHECK(n != nullptr);
        if (!n) continue;
        checkBits64(std::string("normalizationFactor ") + dv.name, bitsOf(n->normalizationFactor()), dv.normalizationFactor);
        checkInt(std::string("octave count ") + dv.name, static_cast<long long>(n->octaves().size()), static_cast<long long>(dv.octaves.size()));
        checkInt(std::string("layer count ") + dv.name, static_cast<long long>(n->layerCount()), static_cast<long long>(dv.octaves.size() * 2));
        if (n->octaves().size() != dv.octaves.size() || n->layerCount() != dv.octaves.size() * 2) continue;
        for (std::size_t o = 0; o < dv.octaves.size(); ++o) {
            const std::string t = std::string(dv.name) + " octave " + std::to_string(dv.octaves[o].octave);
            checkInt(t + " index", n->octaves()[o].octave, dv.octaves[o].octave);
            checkBits64(t + " frequency", bitsOf(n->octaves()[o].frequency), dv.octaves[o].frequency);
            checkBits64(t + " amplitude", bitsOf(n->octaves()[o].amplitude), dv.octaves[o].amplitude);
            checkBits(t + " float amplitude (first)", bitsOf(n->layerAmplitude(2 * o)), dv.octaves[o].floatAmplitude);
            checkBits(t + " float amplitude (second)", bitsOf(n->layerAmplitude(2 * o + 1)), dv.octaves[o].floatAmplitude);
            double freq;
            std::memcpy(&freq, &dv.octaves[o].frequency, sizeof freq);
            checkBits64(t + " second layer frequency", bitsOf(n->layerFrequency(2 * o + 1)), bitsOf(freq * 1.0181268882175227));
        }
    }
    // The first octave of continentalness for seed 12345 [REAL: section 4]: offsets and permutation heads of both layers
    {
        const NormalNoise* n = rs.noise("minecraft:continentalness", err);
        if (n) {
            checkBits64("continentalness first octave_-9 offsetX", bitsOf(n->layerNoise(0).offsetX()), 0x4061c8eb3025e24full);
            checkBits64("continentalness first octave_-9 offsetY", bitsOf(n->layerNoise(0).offsetY()), 0x401d30ea9a0af2e0ull);
            checkBits64("continentalness first octave_-9 offsetZ", bitsOf(n->layerNoise(0).offsetZ()), 0x404b85183d8ee5dcull);
            const int perm1[8] = {14, 158, 73, 15, 250, 129, 33, 152};
            const int perm2[8] = {140, 210, 9, 42, 126, 234, 225, 254};
            for (int i = 0; i < 8; ++i) {
                checkInt("continentalness first perm " + std::to_string(i), n->layerNoise(0).permutation(i), perm1[i]);
                checkInt("continentalness second perm " + std::to_string(i), n->layerNoise(1).permutation(i), perm2[i]);
            }
            checkBits64("continentalness second octave_-9 offsetX", bitsOf(n->layerNoise(1).offsetX()), 0x4069187448aba073ull);
        }
    }
    // Named factories: root.fromHashOf(id).forkPositional()
    {
        const XoroshiroPositionalRandomFactory aq = rs.randomFactory("minecraft:aquifer");
        XoroshiroRandomSource direct = rs.root().makeFromHashOf("minecraft:aquifer");
        const XoroshiroPositionalRandomFactory expect = direct.forkPositionalValue();
        CHECK(aq.lo() == expect.lo() && aq.hi() == expect.hi());
    }
}
