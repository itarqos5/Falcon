// Test vectors for the cryptography of the online login, made with another implementation (Node's crypto): not secrets, nothing but the tests uses this key.
#pragma once

namespace falcon::net::vectors {

// The hash of a login with an empty server id and no secret (what the session server's hash is for the bare name): Java's BigInteger.toString(16) of the SHA-1.
inline constexpr const char* kNotchHash = "4ed1f46bbe04bc756bcb17c0c7ce3e4632f06a48";
inline constexpr const char* kJebHash = "-7c9d5b0044c130109a5d7b5fb5c317c02b4e28c1";
inline constexpr const char* kSimonHash = "88e16a1019277b15d58faf0541e11910eb756f6";

// AES-128/CFB8: key 2b7e1516..., iv 00010203..., 70 bytes of text (byte i is i*37+11 mod 256) and what they encrypt to.
inline constexpr const char* kCfb8Cipher = "5baec3fd0c9314e7b60e7062530e4f933d2da311a5913307c1884be66558a3f57dd4e8628a75d7bc312a495a752730f2e8ab9dc7a380567de3330771e5a32fb88cf461ff91c9";

// An RSA key of 2048 bits: PKCS#8 and X.509 as base64, and a SHA256withRSA signature of "a test message that is signed".
inline constexpr const char* kPrivateKeyBase64 =
    "MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQCvCzs6uSGve0RLOVVmyFPtrRZQ1j07dTEFwF1pw4Z+lU6zWwGg"
    "s15o8G60BDTgpauebM9wdJ1bb8xrHc1T8VS6nuoEYZSKhQ73W3hWROqLN3GLTuCmjM+7hgLjpNSOWe6kbAwDUXzsSmckZhsiviJY"
    "qQ3tHr82B7cR7KVOeep9LG1MZpiyq8yw8kllsU1qAwTEs85Fu5zPnHTy+S10rmdUlZiluZpjEOi1zdozzIFdFWdD7b5Eii42IZXt"
    "gtr/B1bDjG6UEUY4CTlZKintUOikiQBOM0K8G5mv+GQiyR5YFti8dcEQXernFBmLPYgvi+7NyL+BcVdY2rXu0uR6tpivAgMBAAEC"
    "ggEAANu2kqHNvyUvlImCGFrrGRiRf3FNeOPTiwpXwD5Wg9BfmRQyfIM590cWg94cFEM7sC8rD+DCf2xGPaWLwtmavL+CuOVxllbz"
    "V9XO+pKj7m17Kl+x958Wdajr4rSFhkfSmmPH5fbu/ImbhTDVo+Siw88wY5/posOEQPDpbpUScpf/FbqmYHv0QsbusIv+6pDgAEtj"
    "m+KnAdb7zjZwXrt7fruC9VqEFgh68ZuAO34ANoZfe/d3eNYgSfBkvD3RUZpnkf1B++G+BrlCXb3qfr8ehwNczCvcjH5+2qKx0Ub9"
    "YRfdW3Y8d7AispuIcFmaz+0RaGVg3cBbWLSuCQQL2J8h4QKBgQDqLfUAxPqo/+lwlviUqT0bLy1AWu551Euq4/bc0d1ESqh5sdMx"
    "zYCKQXtXclpkeicb9aay2YZZrkWxZSKQ3G6lvyQaXDk/Gvr4CqUz5+8jR/AYA4pErye7FGDupsp6CZRFvAvh5lWeX3Z1XZzQA7AM"
    "J9dbUv96hKcbqcndKeKvFQKBgQC/WqvuqOnoJ5YExjVuetTc97zOOsrVC8mi3WuLMDZmUeNGoO7SXvBBLERMUkF8EbvUmGhr1ycM"
    "xwy/6SFvxmbkwNOi+nO4Iu+k5BIM3Mr65j7Ei/51zOrUJYLqWmwPhuFJLmntkJbc7wAgjbnRgf7wWm266oybL4iFzGfxifC5swKB"
    "gGEdEd9x0KlJB1vjn862qFqfo9puWzs2qPKDexp0BZiOhXOCJR0gJdAv+6B2EbpdVd6obqr4vlhuhwRb9ouwfkpKDATKfgsHe1Bl"
    "ycDOzEMLUCwFO8/j+0v/1XrivFEDq/OHWj4iMRtuZ0egvKke/mDsHMkIkF4UqOA19zq6ZgFJAoGBALDI0R0tuuP7ZpLEl+qY2n3G"
    "9wzErk2DlWa9D7VI9okzUDkZR2rdwTNE9VhZihxYueJgZ+BxpqccYoAOnxCICNl/+EaTAkP1Ktd4yeBOP8zq2MR23cDxb2ZNvZRL"
    "s5foscwNcFnFgktdFw38Qdb4NZLSyJuuQ/a/RG7weHtjSB4XAoGBAJI9BdxqMuBXoUmP48egM2CxupJiaQlg/c6ANr7XT1b/x3Md"
    "2WifrBIMuwTDBCLr+envYhKYrJbGO2HUVZcBWVeceI9PUg8ocVgsk6CscA+eb9RYAAnAHoki2Tu9xR7NItUy4O4xQACl4Te3xIIe"
    "t9JqmMUrXdhfTkRpePi+juCu";
inline constexpr const char* kPublicKeyBase64 =
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEArws7Orkhr3tESzlVZshT7a0WUNY9O3UxBcBdacOGfpVOs1sBoLNeaPBu"
    "tAQ04KWrnmzPcHSdW2/Max3NU/FUup7qBGGUioUO91t4VkTqizdxi07gpozPu4YC46TUjlnupGwMA1F87EpnJGYbIr4iWKkN7R6/"
    "Nge3EeylTnnqfSxtTGaYsqvMsPJJZbFNagMExLPORbucz5x08vktdK5nVJWYpbmaYxDotc3aM8yBXRVnQ+2+RIouNiGV7YLa/wdW"
    "w4xulBFGOAk5WSop7VDopIkATjNCvBuZr/hkIskeWBbYvHXBEF3q5xQZiz2IL4vuzci/gXFXWNq17tLkeraYrwIDAQAB";
inline constexpr const char* kSignatureHex =
    "8de754742dab493c63b1fb690d1e662f49dd68fcef3d97430fd6709181007da0a1c7c2b9d87b828a250d798df99739bb0d4e"
    "011b747539d28cfda6c3c60bbfeaa87bdc4ab9ba617e9c0c8cab801bc75d24f75c38bf586681f39d9457b133ae89a5d4357c"
    "2431a2153bad5b3c368ccd802ede55ac16cfa70d0e1550bec63e3c7ae01db496d14e1b4bfe63192c2e3c43397a4b25ced0df"
    "d34aa95675e36e2b589762fc8349571f61b50d47c98ae792fc8b2f7cff916e28917a22306e82a5e89a15f8598e507ce4e071"
    "ad4f70038742cd5b4bff5a676ae20ccc14fd2e7b3c4e2214034bbddc07f49b6d41fe554cb92f8d18f461d58b02e909c4c8ba"
    "6bb8b55c6809";

// The signature of a chat message with that key: sender 01..10, session 80..8f, number 3, salt 0x0123456789abcdef, time 1700000000 s, the text "h\u00e9llo \u2713" in UTF-8.
inline constexpr const char* kChatSignatureHex =
    "2b3276357fa17e74e247e843bda83247ebfc6ac54efb6c64a71ead23cdd6788e0579b2f74a259bb6067f05393fa0f3c88614"
    "57449201f4b2f0b10fe923bab86551a2ca21ac3afd8c081d9b510e7b61af78339080d355a22a980c3b24aabd2220bd713d4b"
    "293898cdda229030b8a76f8467cedf42bf15f7775aeaa72d707666694837aa459515ce4a033f7b16a971c67a4b8df1c5e2af"
    "be0661c21cd1a97dab01b10f2b8d6418c8e901e0111477cc3d03134ad6efbcd8cde6a3994d03f719675d6de3bd12925a5922"
    "7f22f0cabbcc473c82307c8e18baaac0dba08ef583c47a73f65bce26f88419479900c8b0f378848da991ee07dbbebaaf6e33"
    "0790b3eb62f4";

// The same with two seen messages in the payload (signatures of the bytes 0,1,2... and 255,254,...): the signature of the whole.
inline constexpr const char* kChatSignatureSeenHex =
    "01679c35406dbc2d2a09fb48089a7729af66634b0bf0553655e7e2b92eed55ba061c81366a88c13f8012d03ff3f17296c239"
    "28c94c151c0d50737d68d3373ee7a05f2138126dddfbc63eb6da07171c5a6e8f37e77eb868f50afd8d2a6327ce05bee07b8f"
    "46b419b1e9edc9721521de6f40d9e51ffe54060f7ee337b92ef94778d5bf802d49fd061d17a0b14bcff96bf21f4f1b164eb3"
    "160d0c0a9d553809ec2eefc2e72a9f529aaf7bd7e10719a98165da1a24844833f6d34664ace299f94df4dcf8f228e0a343df"
    "81315b5738d83ae99423d42eaa56d6e04beaacff29b32b30c84e00f02247329d450892371a32d09a16e90013eada6c15a74f"
    "ce5cf54778db";

}  // namespace falcon::net::vectors
