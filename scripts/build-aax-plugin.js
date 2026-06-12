#!/usr/bin/env node
const { spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const repoRoot = path.resolve(__dirname, '..');
const aaxDir = path.join(repoRoot, 'aax');
const sdkRoot = process.env.AAX_SDK_ROOT || '/Users/jamisonrabbe/Downloads/aax-sdk-2-9-0';
const sdkBuildDir = path.join(repoRoot, 'out', 'aax-sdk-build');
const pluginBuildDir = path.join(repoRoot, 'out', 'aax');
const pluginName = 'SimplestSamplerAudioSuite';
const defaultInstallDir =
  '/Library/Application Support/Avid/Audio/Plug-Ins';

function run(cmd, args, opts = {}) {
  const result = spawnSync(cmd, args, { stdio: 'inherit', ...opts });
  if (result.status !== 0) {
    process.exit(result.status || 1);
  }
}

function requirePath(targetPath, hint) {
  if (!fs.existsSync(targetPath)) {
    console.error(hint || `Missing path: ${targetPath}`);
    process.exit(1);
  }
}

function resolveParallelJobs() {
  const raw = String(process.env.AAX_BUILD_JOBS || process.env.CMAKE_BUILD_PARALLEL_LEVEL || '').trim();
  if (/^\d+$/.test(raw)) {
    return Math.max(1, Number(raw));
  }
  return Math.max(1, os.cpus().length);
}

function resolveBuiltPluginPath() {
  const primary = path.join(
    pluginBuildDir,
    'SimplestSamplerAudioSuite',
    'Release',
    `${pluginName}.aaxplugin`
  );
  if (fs.existsSync(primary)) {
    return primary;
  }
  const alt = path.join(pluginBuildDir, 'Release', `${pluginName}.aaxplugin`);
  if (fs.existsSync(alt)) {
    return alt;
  }
  return null;
}

function installPlugin(bundlePath, installDir) {
  const destination = path.join(installDir, `${pluginName}.aaxplugin`);
  if (!fs.existsSync(installDir)) {
    console.error(`Plug-Ins folder not found: ${installDir}`);
    console.error('Pro Tools scans /Library/Application Support/Avid/Audio/Plug-Ins/, not ~/Library/...');
    process.exit(1);
  }

  run('rm', ['-rf', destination]);
  run('cp', ['-R', bundlePath, destination]);
  run('codesign', ['--force', '--deep', '--sign', '-', destination]);
  console.log(`Installed AAX plugin to ${destination}`);
}

function main() {
  if (process.platform !== 'darwin') {
    console.error('SimplestSampler AAX plugin build is macOS-only.');
    process.exit(1);
  }

  requirePath(sdkRoot, `AAX SDK not found at ${sdkRoot}. Set AAX_SDK_ROOT.`);

  fs.mkdirSync(path.dirname(sdkBuildDir), { recursive: true });
  fs.mkdirSync(pluginBuildDir, { recursive: true });

  const configureSdkArgs = [
    '-S', sdkRoot,
    '-B', sdkBuildDir,
    '-G', 'Xcode',
    '-DAAX_BUILD_EXAMPLES=OFF',
  ];
  run('cmake', configureSdkArgs, { cwd: repoRoot, env: { ...process.env, AAX_SDK_ROOT: sdkRoot } });
  run('cmake', ['--build', sdkBuildDir, '--config', 'Release', '--target', 'AAX_Export', 'AAXLibrary'], { cwd: repoRoot });

  const configurePluginArgs = [
    '-S', aaxDir,
    '-B', pluginBuildDir,
    '-G', 'Xcode',
    `-DAAX_SDK_ROOT=${sdkRoot}`,
    `-DAAX_SDK_DIR=${sdkBuildDir}`,
    `-DAAX_SDK_BUILD_DIR=${sdkBuildDir}`,
  ];
  const cmakeOutputDir = process.env.AAX_PLUGIN_OUTPUT_DIRECTORY || '';
  if (cmakeOutputDir) {
    configurePluginArgs.push(`-DAAX_SDK_PLUGIN_OUTPUT_DIRECTORY=${cmakeOutputDir}`);
  }
  run('cmake', configurePluginArgs, { cwd: repoRoot, env: { ...process.env, AAX_SDK_ROOT: sdkRoot } });

  const buildArgs = [
    '--build', pluginBuildDir,
    '--config', 'Release',
    '--target', pluginName,
    '--parallel', String(resolveParallelJobs()),
  ];
  run('cmake', buildArgs, { cwd: repoRoot });

  const builtPlugin = resolveBuiltPluginPath();
  if (!builtPlugin) {
    console.error(`Expected plugin bundle not found under ${pluginBuildDir}`);
    process.exit(1);
  }
  console.log(`Built AAX plugin at ${builtPlugin}`);

  const installDir =
    process.env.AAX_PLUGIN_INSTALL_DIRECTORY ||
    process.env.AAX_PLUGIN_OUTPUT_DIRECTORY ||
    defaultInstallDir;
  if (process.env.AAX_SKIP_INSTALL === '1') {
    console.log(`Skipping install (AAX_SKIP_INSTALL=1). Copy manually to ${defaultInstallDir}`);
    return;
  }
  installPlugin(builtPlugin, installDir);
}

main();
