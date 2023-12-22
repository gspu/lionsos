const std = @import("std");
const builtin = @import("builtin");

// var general_purpose_allocator = std.heap.GeneralPurposeAllocator(.{}){};
// const gpa = general_purpose_allocator.allocator();

var libmicrokit: std.Build.LazyPath = undefined;
var libmicrokit_linker_script: std.Build.LazyPath = undefined;
var libmicrokit_include: std.Build.LazyPath = undefined;

// @ivanv: ideally this would just be gone
const no_ub_santize = [_][]const u8{ "-fno-sanitize=undefined" };

fn addMicrokitProtectionDomain(b: *std.Build, options: std.Build.ExecutableOptions) *std.Build.Step.Compile {
    const pd = b.addExecutable(options);
    pd.addObjectFile(libmicrokit);
    pd.setLinkerScriptPath(libmicrokit_linker_script);
    pd.addIncludePath(libmicrokit_include);

    return pd;
}

const micropython_port_sources = [_][]const u8{
    "src/micropython/micropython.c",
    "src/micropython/modfb.c",
    "src/micropython/modmyport.c",
    "src/micropython/modtime.c",
    "src/micropython/mphalport.c",
    "src/micropython/sddf_fs.c",
    "src/micropython/vfs_sddf_fs.c",
    "src/micropython/vfs_sddf_fs_file.c",
    "../../micropython/extmod/modtime.c",
	"../../micropython/extmod/vfs.c",
	"../../micropython/extmod/modos.c",
	"../../micropython/extmod/vfs_reader.c",
    "../../micropython/shared/readline/readline.c",
	"../../micropython/shared/runtime/gchelper_generic.c",
	"../../micropython/shared/runtime/pyexec.c",
	"../../micropython/shared/runtime/stdout_helpers.c",
	// "../../micropython/shared/libc/string0.c",
	// "../../micropython/shared/libc/printf.c",
	// "../../micropython/shared/libc/__errno.c",
};

const micropython_sources = [_][]const u8{
    "../../micropython/py/mpstate.c",
	"../../micropython/py/nlr.c",
	"../../micropython/py/nlrx86.c",
	"../../micropython/py/nlrx64.c",
	"../../micropython/py/nlrthumb.c",
	"../../micropython/py/nlraarch64.c",
	"../../micropython/py/nlrmips.c",
	"../../micropython/py/nlrpowerpc.c",
	"../../micropython/py/nlrxtensa.c",
	"../../micropython/py/nlrsetjmp.c",
	"../../micropython/py/malloc.c",
	"../../micropython/py/gc.c",
	"../../micropython/py/pystack.c",
	"../../micropython/py/qstr.c",
	"../../micropython/py/vstr.c",
	"../../micropython/py/mpprint.c",
	"../../micropython/py/unicode.c",
	"../../micropython/py/mpz.c",
	"../../micropython/py/reader.c",
	"../../micropython/py/lexer.c",
	"../../micropython/py/parse.c",
	"../../micropython/py/scope.c",
	"../../micropython/py/compile.c",
	"../../micropython/py/emitcommon.c",
	"../../micropython/py/emitbc.c",
	"../../micropython/py/asmbase.c",
	"../../micropython/py/asmx64.c",
	"../../micropython/py/emitnx64.c",
	"../../micropython/py/asmx86.c",
	"../../micropython/py/emitnx86.c",
	"../../micropython/py/asmthumb.c",
	"../../micropython/py/emitnthumb.c",
	"../../micropython/py/emitinlinethumb.c",
	"../../micropython/py/asmarm.c",
	"../../micropython/py/emitnarm.c",
	"../../micropython/py/asmxtensa.c",
	"../../micropython/py/emitnxtensa.c",
	"../../micropython/py/emitinlinextensa.c",
	"../../micropython/py/emitnxtensawin.c",
	"../../micropython/py/formatfloat.c",
	"../../micropython/py/parsenumbase.c",
	"../../micropython/py/parsenum.c",
	"../../micropython/py/emitglue.c",
	"../../micropython/py/persistentcode.c",
	"../../micropython/py/runtime.c",
	"../../micropython/py/runtime_utils.c",
	"../../micropython/py/scheduler.c",
	"../../micropython/py/nativeglue.c",
	"../../micropython/py/pairheap.c",
	"../../micropython/py/ringbuf.c",
	"../../micropython/py/stackctrl.c",
	"../../micropython/py/argcheck.c",
	"../../micropython/py/warning.c",
	"../../micropython/py/profile.c",
	"../../micropython/py/map.c",
	"../../micropython/py/obj.c",
	"../../micropython/py/objarray.c",
	"../../micropython/py/objattrtuple.c",
	"../../micropython/py/objbool.c",
	"../../micropython/py/objboundmeth.c",
	"../../micropython/py/objcell.c",
	"../../micropython/py/objclosure.c",
	"../../micropython/py/objcomplex.c",
	"../../micropython/py/objdeque.c",
	"../../micropython/py/objdict.c",
	"../../micropython/py/objenumerate.c",
	"../../micropython/py/objexcept.c",
	"../../micropython/py/objfilter.c",
	"../../micropython/py/objfloat.c",
	"../../micropython/py/objfun.c",
	"../../micropython/py/objgenerator.c",
	"../../micropython/py/objgetitemiter.c",
	"../../micropython/py/objint.c",
	"../../micropython/py/objint_longlong.c",
	"../../micropython/py/objint_mpz.c",
	"../../micropython/py/objlist.c",
	"../../micropython/py/objmap.c",
	"../../micropython/py/objmodule.c",
	"../../micropython/py/objobject.c",
	"../../micropython/py/objpolyiter.c",
	"../../micropython/py/objproperty.c",
	"../../micropython/py/objnone.c",
	"../../micropython/py/objnamedtuple.c",
	"../../micropython/py/objrange.c",
	"../../micropython/py/objreversed.c",
	"../../micropython/py/objset.c",
	"../../micropython/py/objsingleton.c",
	"../../micropython/py/objslice.c",
	"../../micropython/py/objstr.c",
	"../../micropython/py/objstrunicode.c",
	"../../micropython/py/objstringio.c",
	"../../micropython/py/objtuple.c",
	"../../micropython/py/objtype.c",
	"../../micropython/py/objzip.c",
	"../../micropython/py/opmethods.c",
	"../../micropython/py/sequence.c",
	"../../micropython/py/stream.c",
	"../../micropython/py/binary.c",
	"../../micropython/py/builtinimport.c",
	"../../micropython/py/builtinevex.c",
	"../../micropython/py/builtinhelp.c",
	"../../micropython/py/modarray.c",
	"../../micropython/py/modbuiltins.c",
	"../../micropython/py/modcollections.c",
	"../../micropython/py/modgc.c",
	"../../micropython/py/modio.c",
	"../../micropython/py/modmath.c",
	"../../micropython/py/modcmath.c",
	"../../micropython/py/modmicropython.c",
	"../../micropython/py/modstruct.c",
	"../../micropython/py/modsys.c",
	"../../micropython/py/moderrno.c",
	"../../micropython/py/modthread.c",
	"../../micropython/py/vm.c",
	"../../micropython/py/bc.c",
	"../../micropython/py/showbc.c",
	"../../micropython/py/repl.c",
	"../../micropython/py/smallint.c",
	"../../micropython/py/frozenmod.c",
};

pub fn build(b: *std.Build) !void {
    const optimize = b.standardOptimizeOption(.{});
    const target = std.zig.CrossTarget{
        .cpu_arch = .aarch64,
        .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_a55 },
        .os_tag = .freestanding,
        .abi = .none,
    };

    const board = "odroidc4";
    const config = "debug";

    // Depending on the host, we need a different Microkit SDK. Right now
    // only Linux x64 and macOS x64/ARM64 are supported, so we need to check
    // what platform the person compiling the project is using.
    const microkit_sdk_name = switch (builtin.target.os.tag) {
        .linux => switch (builtin.target.cpu.arch) {
            .x86_64 => "microkit_linux_x64",
            else => {
                std.debug.print("ERROR: only x64 is supported on Linux.", .{});
                std.os.exit(1);
            }
        },
        .macos => switch (builtin.target.cpu.arch) {
            .x86_64 => "microkit_macos_x64",
            .aarch64 => "microkit_macos_arm64",
            else => {
                std.debug.print("ERROR: only x64 and ARM64 are supported on macOS.", .{});
                std.os.exit(1);
            }
        },
        else => {
            std.debug.print("ERROR: OS '{s}' is not supported.", .{ builtin.target.os.tag });
        }
    };

    const microkit = b.dependency(microkit_sdk_name, .{});
    const sddf = b.dependency("sddf", .{});
    const picolibc_dep = b.dependency("picolibc", .{
        .target = target,
        .optimize = optimize,
    });
    const libco_dep = b.dependency("libco", .{
        .target = target,
        .optimize = optimize,
    });

    const microkit_board_dir = "board/" ++ board ++ "/" ++ config;
    libmicrokit = microkit.path(microkit_board_dir ++ "/lib/libmicrokit.a");
    libmicrokit_linker_script = microkit.path(microkit_board_dir ++ "/lib/microkit.ld");
    libmicrokit_include = microkit.path(microkit_board_dir ++ "/include");

    const picolibc = picolibc_dep.artifact("c");

    const timer_protocol = b.addObject(.{
        .name = "sddf_timer_protocol",
        .target = target,
        .optimize = optimize,
    });
    timer_protocol.addCSourceFile(.{ .file = sddf.path("timer/client/client.c"), .flags = &.{ "-DTIMER_CHANNEL=2" }});
    timer_protocol.addIncludePath(sddf.path("include"));
    timer_protocol.addIncludePath(libmicrokit_include);

    const serial_ring = b.addObject(.{
        .name = "sddf_serial_shared_ring_buffer",
        .target = target,
        .optimize = optimize,
    });
    serial_ring.addCSourceFile(.{
        .file = sddf.path("serial/libserialsharedringbuffer/shared_ringbuffer.c"),
        .flags = &no_ub_santize
    });
    serial_ring.addIncludePath(sddf.path("include"));
    serial_ring.addIncludePath(sddf.path("util/include"));

    const ethernet_ring = b.addObject(.{
        .name = "sddf_ethernet_shared_ring_buffer",
        .target = target,
        .optimize = optimize,
    });
    ethernet_ring.addCSourceFile(.{ .file = sddf.path("network/libethsharedringbuffer/shared_ringbuffer.c"), .flags = &no_ub_santize });
    ethernet_ring.addIncludePath(libmicrokit_include);
    ethernet_ring.addIncludePath(sddf.path("include"));
    ethernet_ring.addIncludePath(sddf.path("util/include"));

    const uart_driver = addMicrokitProtectionDomain(b, .{
        .name = "uart_driver.elf",
        .target = target,
        .optimize = optimize,
    });
    uart_driver.addCSourceFile(.{ .file = sddf.path("drivers/serial/meson/uart.c"), .flags = &.{} });
    uart_driver.addIncludePath(sddf.path("drivers/serial/meson/include"));
    uart_driver.addIncludePath(sddf.path("include"));
    uart_driver.addIncludePath(sddf.path("util/include"));
    uart_driver.addObject(serial_ring);
    b.installArtifact(uart_driver);

    const timer_driver = addMicrokitProtectionDomain(b, .{
        .name = "timer_driver.elf",
        .target = target,
        .optimize = optimize,
    });
    timer_driver.addCSourceFile(.{ .file = sddf.path("drivers/clock/meson/timer.c"), .flags = &.{} });
    timer_driver.addIncludePath(sddf.path("drivers/serial/meson/include"));
    b.installArtifact(timer_driver);

    const serial_component_flags = [_][]const u8{
        "-DSERIAL_NUM_CLIENTS=1"
    };

    const serial_mux_rx = addMicrokitProtectionDomain(b, .{
        .name = "serial_mux_rx.elf",
        .target = target,
        .optimize = optimize,
    });
    serial_mux_rx.addCSourceFile(.{ .file = sddf.path("serial/components/mux_rx.c"), .flags = &serial_component_flags });
    serial_mux_rx.addIncludePath(sddf.path("drivers/serial/meson/include"));
    serial_mux_rx.addIncludePath(sddf.path("include"));
    serial_mux_rx.addIncludePath(sddf.path("util/include"));
    serial_mux_rx.addObject(serial_ring);
    b.installArtifact(serial_mux_rx);

    const serial_mux_tx = addMicrokitProtectionDomain(b, .{
        .name = "serial_mux_tx.elf",
        .target = target,
        .optimize = optimize,
    });
    serial_mux_tx.addCSourceFile(.{ .file = sddf.path("serial/components/mux_tx.c"), .flags = &serial_component_flags });
    serial_mux_tx.addIncludePath(sddf.path("drivers/serial/meson/include"));
    serial_mux_tx.addIncludePath(sddf.path("include"));
    serial_mux_tx.addIncludePath(sddf.path("util/include"));
    serial_mux_tx.addObject(serial_ring);
    b.installArtifact(serial_mux_tx);

    const ethernet_driver = addMicrokitProtectionDomain(b, .{
        .name = "ethernet_driver.elf",
        .target = target,
        .optimize = optimize,
    });
    ethernet_driver.addCSourceFile(.{ .file = sddf.path("drivers/network/meson/ethernet.c"), .flags = &no_ub_santize });
    ethernet_driver.addIncludePath(sddf.path("include"));
    ethernet_driver.addObject(ethernet_ring);
    b.installArtifact(ethernet_driver);

    const ethernet_mux_rx = addMicrokitProtectionDomain(b, .{
        .name = "ethernet_mux_rx.elf",
        .target = target,
        .optimize = optimize,
    });
    ethernet_mux_rx.addCSourceFile(.{ .file = sddf.path("network/components/mux_rx.c"), .flags = &.{} });
    ethernet_mux_rx.addIncludePath(sddf.path("include"));
    ethernet_mux_rx.addIncludePath(sddf.path("util/include"));
    ethernet_mux_rx.addIncludePath(sddf.path("network/ipstacks/lwip/src/include"));
    ethernet_mux_rx.addObject(ethernet_ring);
    ethernet_mux_rx.addIncludePath(picolibc_dep.path(""));
    ethernet_mux_rx.addIncludePath(picolibc_dep.path("newlib/libc/include"));
    ethernet_mux_rx.addIncludePath(picolibc_dep.path("newlib/libc/tinystdio"));
    ethernet_mux_rx.linkLibrary(picolibc);
    b.installArtifact(ethernet_mux_rx);

    const ethernet_mux_tx = addMicrokitProtectionDomain(b, .{
        .name = "ethernet_mux_tx.elf",
        .target = target,
        .optimize = optimize,
    });
    ethernet_mux_tx.addCSourceFile(.{ .file = sddf.path("network/components/mux_tx.c"), .flags = &.{} });
    ethernet_mux_tx.addIncludePath(sddf.path("include"));
    ethernet_mux_tx.addIncludePath(sddf.path("util/include"));
    ethernet_mux_tx.addObject(ethernet_ring);
    b.installArtifact(ethernet_mux_tx);

    const ethernet_arp = addMicrokitProtectionDomain(b, .{
        .name = "ethernet_arp.elf",
        .target = target,
        .optimize = optimize,
    });
    ethernet_arp.addCSourceFile(.{ .file = sddf.path("network/components/arp.c"), .flags = &.{} });
    ethernet_arp.addCSourceFile(.{ .file = sddf.path("network/ipstacks/lwip/src/core/inet_chksum.c"), .flags = &.{} });
    ethernet_arp.addCSourceFile(.{ .file = sddf.path("network/ipstacks/lwip/src/core/def.c"), .flags = &.{} });
    ethernet_arp.addIncludePath(sddf.path("include"));
    ethernet_arp.addIncludePath(sddf.path("util/include"));
    ethernet_arp.addIncludePath(sddf.path("network/ipstacks/lwip/src/include"));
    ethernet_arp.linkLibrary(picolibc);
    ethernet_arp.addIncludePath(picolibc_dep.path(""));
    ethernet_arp.addIncludePath(picolibc_dep.path("newlib/libc/include"));
    ethernet_arp.addIncludePath(picolibc_dep.path("newlib/libc/tinystdio"));
    ethernet_arp.addObject(ethernet_ring);
    b.installArtifact(ethernet_arp);

    const ethernet_copy = addMicrokitProtectionDomain(b, .{
        .name = "ethernet_copy.elf",
        .target = target,
        .optimize = optimize,
    });
    ethernet_copy.addCSourceFile(.{ .file = sddf.path("network/components/copy.c"), .flags = &.{} });
    ethernet_copy.addIncludePath(sddf.path("include"));
    ethernet_copy.addIncludePath(sddf.path("util/include"));
    ethernet_copy.linkLibrary(picolibc);
    ethernet_copy.addIncludePath(picolibc_dep.path(""));
    ethernet_copy.addIncludePath(picolibc_dep.path("newlib/libc/include"));
    ethernet_copy.addObject(ethernet_ring);
    b.installArtifact(ethernet_copy);

    const micropython = addMicrokitProtectionDomain(b, .{
        .name = "micropython.elf",
        .target = target,
        .optimize = optimize,
    });
    micropython.addCSourceFiles(.{ .files = &micropython_sources, .flags = &no_ub_santize });
    micropython.addCSourceFiles(.{ .files = &micropython_port_sources, .flags = &no_ub_santize });
    micropython.addCSourceFile(.{ .file = .{ .path = "../../fs/protocol/protocol.c" }, .flags = &.{} });
    micropython.addIncludePath(picolibc_dep.path(""));
    micropython.addIncludePath(picolibc_dep.path("newlib/libc/include"));
    micropython.addIncludePath(picolibc_dep.path("newlib/libc/tinystdio"));
    micropython.addIncludePath(.{ .path = "../../micropython" });
    micropython.addIncludePath(.{ .path = "src/micropython" });
    micropython.addIncludePath(.{ .path = "../../fs/include" });
    // @ivanv: hack!
    micropython.addIncludePath(.{ .path = "temp_artefacts" });
    micropython.addIncludePath(sddf.path("include"));
    micropython.addIncludePath(sddf.path("util/include"));
    micropython.linkLibrary(libco_dep.artifact("co"));
    micropython.linkLibrary(picolibc_dep.artifact("c"));
    micropython.addObject(serial_ring);
    micropython.addObject(timer_protocol);
    b.installArtifact(micropython);

    const sdf = "kitty_zig.system";
    const system_image = b.getInstallPath(.bin, "./kitty.img");

    const microkit_tool = microkit.path("bin/microkit").getPath(b);
    // Until https://github.com/ziglang/zig/issues/17462 is solved, the Zig build
    // system does not respect the executable mode of dependencies, this affects
    // using the Microkit SDK since the tool is expected to be executable.
    // For now, we manually make it executable ourselves.
    const microkit_tool_chmod = b.addSystemCommand(&[_][]const u8{ "chmod", "+x", microkit_tool });

    // Setup the defualt build step which will take our hello world ELF and build the final system
    // image using the Microkit tool.
    const microkit_tool_cmd = b.addSystemCommand(&[_][]const u8{
       microkit_tool,
       sdf,
       "--search-path",
       b.getInstallPath(.bin, ""),
       "--board",
       board,
       "--config",
       config,
       "-o",
       system_image,
       "-r",
       b.getInstallPath(.prefix, "./report.txt")
    });
    microkit_tool_cmd.step.dependOn(&microkit_tool_chmod.step);
    microkit_tool_cmd.step.dependOn(b.getInstallStep());
    const microkit_step = b.step("microkit", "Compile and build the bootable system image");
    microkit_step.dependOn(&microkit_tool_cmd.step);
    b.default_step = microkit_step;
}
