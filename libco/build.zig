const std = @import("std");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const libco = b.addStaticLibrary(.{
        .name = "co",
        .target = target,
        .optimize = optimize,
    });

    const picolibc = b.dependency("picolibc", .{
        .target = target,
        .optimize = optimize,
    });

    switch (target.result.cpu.arch) {
        .x86_64, .aarch64, .arm => {},
        else => {
            std.debug.print("Unexpected target architecture for libco: {}\n", .{ target.result.cpu.arch });
            return error.UnexpectedCpuArch;
        }
    }
    // @ivanv: any flags to compile with?
    libco.addCSourceFile(.{ .file = .{ .path = "libco.c" }, .flags = &.{}});
    libco.addIncludePath(.{ .path = "" });
    libco.addIncludePath(picolibc.path(""));
    libco.addIncludePath(picolibc.path("newlib/libc/include"));

    libco.installHeader("libco.h", "libco.h");
    b.installArtifact(libco);
}
