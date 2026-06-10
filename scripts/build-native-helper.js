#!/usr/bin/env node
const { spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const repoRoot = path.resolve(__dirname, '..');
const nativeDir = path.join(repoRoot, 'native');
const descriptorHeaderPath = path.join(nativeDir, 'generated', 'ptsl_descriptor.hpp');

function run(cmd, args, opts = {}) {
  const result = spawnSync(cmd, args, { stdio: 'inherit', ...opts });
  if (result.status !== 0) {
    process.exit(result.status || 1);
  }
}

function requireDir(dir, installHint) {
  if (!fs.existsSync(dir)) {
    console.error(installHint);
    process.exit(1);
  }
}

function requireCommand(commandName, installHint) {
  const probe = spawnSync('xcrun', ['--find', commandName], { encoding: 'utf8' });
  if (probe.status === 0) {
    const resolvedPath = String(probe.stdout || '').trim();
    if (resolvedPath && fs.existsSync(resolvedPath)) {
      return resolvedPath;
    }
  }

  const fallback = spawnSync('which', [commandName], { encoding: 'utf8' });
  if (fallback.status === 0) {
    const resolvedPath = String(fallback.stdout || '').trim();
    if (resolvedPath && fs.existsSync(resolvedPath)) {
      return resolvedPath;
    }
  }

  console.error(installHint);
  process.exit(1);
}

function resolveBuildParallelLevel(targetArch) {
  const buildParallelLevelRaw = String(
    process.env.HELPER_BUILD_JOBS
    || process.env.CMAKE_BUILD_PARALLEL_LEVEL
    || ''
  ).trim();
  if (/^\d+$/.test(buildParallelLevelRaw)) {
    return Math.max(1, Number(buildParallelLevelRaw));
  }

  const totalMemoryGiB = os.totalmem() / (1024 ** 3);
  const cpuCount = Math.max(1, os.cpus().length);

  // Older Intel Macs can run out of memory when gRPC/BoringSSL builds fan out too hard.
  if (targetArch === 'x64' && totalMemoryGiB <= 16.5) {
    return Math.min(2, cpuCount);
  }

  return null;
}

function main() {
  const targetArchRaw = process.env.TARGET_ARCH || process.env.npm_config_arch || process.arch;
  const targetArch = targetArchRaw === 'arm64' || targetArchRaw === 'x64' ? targetArchRaw : process.arch;
  const cmakeArch = targetArch === 'x64' ? 'x86_64' : targetArch;
  const deploymentTarget = String(
    process.env.MACOSX_DEPLOYMENT_TARGET
    || process.env.CMAKE_OSX_DEPLOYMENT_TARGET
    || (targetArch === 'x64' ? '12.0' : '')
  ).trim();
  const useSystemGrpc = /^(1|true|yes|on)$/i.test(String(process.env.PTSL_USE_SYSTEM_GRPC || '').trim());
  const protocolVersionRaw = String(process.env.PTSL_PROTOCOL_VERSION || '').trim();
  const protocolVersionMatch = protocolVersionRaw
    ? /^(\d+)\.(\d+)(?:\.(\d+))?$/.exec(protocolVersionRaw)
    : null;
  const buildParallelLevel = resolveBuildParallelLevel(targetArch);

  if (process.platform !== 'darwin') {
    console.error('The native Pro Tools helper is currently wired for macOS only.');
    process.exit(1);
  }

  if (targetArch !== process.arch) {
    console.error(
      `TARGET_ARCH=${targetArch} does not match the current runtime architecture (${process.arch}). ` +
      'Build the helper from a shell running under the target architecture.'
    );
    process.exit(1);
  }

  const clangPath = requireCommand(
    'clang',
    'A macOS C/C++ toolchain is required. Install Xcode Command Line Tools with `xcode-select --install`.'
  );
  const clangxxPath = requireCommand(
    'clang++',
    'A macOS C/C++ toolchain is required. Install Xcode Command Line Tools with `xcode-select --install`.'
  );
  requireCommand(
    'cmake',
    'CMake is required to build the native helper. Install it with `brew install cmake`.'
  );
  requireDir(descriptorHeaderPath,
    'Missing native/generated/ptsl_descriptor.hpp. Restore it from the repo or regenerate it before building.');

  let grpcSourceDir = '';
  if (!useSystemGrpc) {
    grpcSourceDir = path.join(repoRoot, 'out', 'deps', 'grpc');
    if (!fs.existsSync(path.join(grpcSourceDir, 'CMakeLists.txt'))) {
      fs.mkdirSync(path.dirname(grpcSourceDir), { recursive: true });
      run('git', [
        'clone',
        '--depth', '1',
        '--branch', 'v1.71.0',
        '--recurse-submodules',
        '--shallow-submodules',
        'https://github.com/grpc/grpc.git',
        grpcSourceDir,
      ], { cwd: repoRoot });
    }
  }

  const buildDir = path.join(repoRoot, 'out', 'native', targetArch);
  fs.mkdirSync(buildDir, { recursive: true });

  const cmakeArgs = [
    '-S', nativeDir,
    '-B', buildDir,
    '-DCMAKE_BUILD_TYPE=Release',
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
    `-DCMAKE_C_COMPILER=${clangPath}`,
    `-DCMAKE_CXX_COMPILER=${clangxxPath}`,
    `-DCMAKE_ASM_COMPILER=${clangPath}`,
    `-DCMAKE_OSX_ARCHITECTURES=${cmakeArch}`,
  ];
  if (grpcSourceDir) {
    cmakeArgs.push(`-DPTSL_GRPC_SOURCE_DIR=${grpcSourceDir}`);
  }
  if (deploymentTarget) {
    cmakeArgs.push(`-DCMAKE_OSX_DEPLOYMENT_TARGET=${deploymentTarget}`);
  }
  if (useSystemGrpc) {
    cmakeArgs.push('-DPTSL_USE_SYSTEM_GRPC=ON');
  }
  if (protocolVersionMatch) {
    cmakeArgs.push(`-DPTSL_PROTOCOL_VERSION_MAJOR=${protocolVersionMatch[1]}`);
    cmakeArgs.push(`-DPTSL_PROTOCOL_VERSION_MINOR=${protocolVersionMatch[2]}`);
    cmakeArgs.push(`-DPTSL_PROTOCOL_VERSION_REVISION=${protocolVersionMatch[3] || '0'}`);
  }

  run('cmake', cmakeArgs, { cwd: repoRoot });
  const buildArgs = [
    '--build',
    buildDir,
    '--config',
    'Release',
    '--target',
    'ptsl_markers_helper',
    'cue_global_hotkey_helper'
  ];
  if (buildParallelLevel != null) {
    console.log(
      `Using ${buildParallelLevel} parallel build job${buildParallelLevel === 1 ? '' : 's'} for the native helper build. ` +
      'Override with HELPER_BUILD_JOBS or CMAKE_BUILD_PARALLEL_LEVEL if needed.'
    );
    buildArgs.push('--parallel', String(buildParallelLevel));
  }
  run('cmake', buildArgs, { cwd: repoRoot });

  const destDir = path.join(repoRoot, 'bin', `mac-${targetArch}`);
  fs.mkdirSync(destDir, { recursive: true });
  const binaries = [
    'ptsl_markers_helper',
    'cue_global_hotkey_helper'
  ];

  binaries.forEach((binaryName) => {
    const builtBinary = path.join(buildDir, binaryName);
    if (!fs.existsSync(builtBinary)) {
      console.error(`Expected helper binary not found at ${builtBinary}`);
      process.exit(1);
    }

    const destBinary = path.join(destDir, binaryName);
    fs.copyFileSync(builtBinary, destBinary);
    fs.chmodSync(destBinary, 0o755);
    run('codesign', ['--force', '--sign', '-', destBinary], { cwd: repoRoot });
    console.log(`Native helper prepared at ${destBinary}`);
  });
}

main();
