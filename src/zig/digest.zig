// Pure-Zig FNV-1a engine, compiled into the native C++ application when Zig is installed.
export fn jamila_fingerprint(bytes: [*]const u8, len: usize) u64 {
    var hash: u64 = 14695981039346656037;
    for (0..len) |i| {
        hash = (hash ^ @as(u64, bytes[i])) *% 1099511628211;
    }
    return hash;
}

test "known FNV-1a vector" {
    const testing = @import("std").testing;
    try testing.expectEqual(@as(u64, 0xaf63dc4c8601ec8c), jamila_fingerprint("a".ptr, 1));
}
