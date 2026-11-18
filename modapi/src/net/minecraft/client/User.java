package net.minecraft.client;

import java.util.UUID;
import net.falcon.Host;

public class User {
    public String getName() { return Host.playerName(); }
    public String getAccessToken() { return ""; }
    public UUID getProfileId() { String u = Host.playerUuid(); return u.isEmpty() ? new UUID(0L, 0L) : UUID.fromString(u); }
}
