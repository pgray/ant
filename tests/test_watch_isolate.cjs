const assert = require('assert');
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const net = require('net');

function freePort() {
  return new Promise(resolve => {
    const server = net.createServer().listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      server.close(() => resolve(port));
    });
  });
}

function runScenario(name, flag, files, steps, env = {}) {
  return new Promise((resolve, reject) => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), `ant-${name}-`));
    for (const [file, source] of Object.entries(files)) fs.writeFileSync(path.join(dir, file), source);

    const child = spawn(process.execPath, [flag, '--no-clear-screen', path.join(dir, 'entry.mjs')], {
      env: { ...process.env, ...env }
    });
    child.stdout.setEncoding('utf8');
    child.stderr.setEncoding('utf8');

    let stdout = '';
    let stderr = '';
    let step = 0;
    let finished = false;

    const finish = error => {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      child.kill('SIGKILL');
      fs.rmSync(dir, { recursive: true, force: true });
      if (error) reject(error);
      else resolve();
    };

    const timer = setTimeout(() => {
      finish(new Error(`${name} timed out at step ${step}\nstdout:\n${stdout}\nstderr:\n${stderr}`));
    }, 8000);

    const advance = () => {
      while (step < steps.length && (stdout + stderr).includes(steps[step].waitFor)) {
        const { write, rename } = steps[step++];
        if (write) for (const [file, source] of Object.entries(write)) fs.writeFileSync(path.join(dir, file), source);
        if (rename) fs.renameSync(path.join(dir, rename[0]), path.join(dir, rename[1]));
      }
      if (step === steps.length) child.kill('SIGINT');
    };

    child.stdout.on('data', chunk => {
      stdout += chunk;
      advance();
    });

    child.stderr.on('data', chunk => {
      stderr += chunk;
      advance();
    });

    child.on('close', code => {
      if (finished) return;
      try {
        assert.strictEqual(step, steps.length, `${name} stopped at step ${step}\nstdout:\n${stdout}\nstderr:\n${stderr}`);
        assert.strictEqual(code, 130, `${name} exit code ${code}\nstderr:\n${stderr}`);
        assert(!stderr.includes('Script execution interrupted'), `${name} printed a termination\nstderr:\n${stderr}`);
        finish();
      } catch (error) {
        finish(error);
      }
    });
  });
}

const scenarios = [
  () => runScenario('watch-busy', '--watch', {
    'dep.mjs': 'export const v = 1;\n',
    'entry.mjs': "import { v } from './dep.mjs';\nconsole.log('run:' + v);\nif (v === 1) { let i = 0; while (true) i++; }\nsetInterval(() => {}, 1000);\n"
  }, [
    { waitFor: 'run:1', write: { 'dep.mjs': 'export const v = 2;\n' } },
    { waitFor: 'run:2' }
  ]),

  () => runScenario('watch-exit', '--watch', {
    'dep.mjs': 'export const v = 1;\n',
    'entry.mjs': "import { v } from './dep.mjs';\nglobalThis.runs = (globalThis.runs ?? 0) + 1;\nprocess.on('exit', code => console.log('exit:' + code));\nconsole.log('run:' + v + ':' + globalThis.runs);\ntry { process.exit(3); } catch {}\nconsole.log('unreachable');\n"
  }, [
    { waitFor: 'exit:3', write: { 'dep.mjs': 'export const v = 2;\n' } },
    { waitFor: 'run:2:1' }
  ]),

  () => runScenario('watch-atomic-save', '--watch', {
    'dep.mjs': 'export const v = 1;\n',
    'entry.mjs': "import { v } from './dep.mjs';\nconsole.log('run:' + v);\nsetInterval(() => {}, 1000);\n"
  }, [
    { waitFor: 'run:1', write: { 'dep.tmp': 'export const v = 2;\n' }, rename: ['dep.tmp', 'dep.mjs'] },
    { waitFor: 'run:2' }
  ]),

  () => runScenario('hot-state', '--hot', {
    'dep.mjs': 'export const v = 1;\n',
    'entry.mjs': "import { v } from './dep.mjs';\nglobalThis.count = (globalThis.count ?? 0) + 1;\nconsole.log('eval:' + v + ':' + globalThis.count);\n"
  }, [
    { waitFor: 'eval:1:1', write: { 'dep.mjs': 'export const v = ;\n' } },
    { waitFor: 'SyntaxError', write: { 'dep.mjs': 'export const v = 2;\n' } },
    { waitFor: 'eval:2:2' }
  ]),

  async () => runScenario('hot-serve', '--hot', {
    'dep.mjs': 'export const v = 1;\n',
    'entry.mjs': [
      "import { v } from './dep.mjs';",
      "const port = Number(process.env.ANT_TEST_PORT);",
      "const server = Ant.serve({ port, hostname: '127.0.0.1', fetch: () => new Response('v' + v) });",
      "globalThis.first ??= server;",
      "setTimeout(async () => {",
      "  const res = await fetch(`http://127.0.0.1:${port}`, { headers: { connection: 'close' } });",
      "  console.log('serve:' + (await res.text()) + ':' + (globalThis.first === server));",
      "}, 50);",
      ''
    ].join('\n')
  }, [
    { waitFor: 'serve:v1:true', write: { 'dep.mjs': 'export const v = 2;\n' } },
    { waitFor: 'serve:v2:true' }
  ], { ANT_TEST_PORT: String(await freePort()) }),

  async () => runScenario('hot-net', '--hot', {
    'dep.mjs': 'export const v = 1;\n',
    'entry.mjs': [
      "import net from 'node:net';",
      "import { v } from './dep.mjs';",
      "const port = Number(process.env.ANT_TEST_PORT);",
      "const server = net.createServer(socket => socket.end('v' + v));",
      "server.on('close', () => console.log('closed:' + v));",
      "server.listen(port, '127.0.0.1', () => {",
      "  const socket = net.connect(port, '127.0.0.1');",
      "  let data = '';",
      "  socket.on('data', chunk => data += chunk);",
      "  socket.on('end', () => console.log('net:' + data));",
      "});",
      ''
    ].join('\n')
  }, [
    { waitFor: 'net:v1', write: { 'dep.mjs': 'export const v = 2;\n' } },
    { waitFor: 'closed:1' },
    { waitFor: 'net:v2' }
  ], { ANT_TEST_PORT: String(await freePort()) })
];

(async () => {
  for (const scenario of scenarios) await scenario();
  console.log('watch isolate and hot reload ok');
})();
