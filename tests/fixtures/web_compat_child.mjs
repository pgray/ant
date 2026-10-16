import cond from 'web-cond';
import order from 'web-order';
import field from 'web-field';
import map from 'web-map';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const url = f => { try { return f(); } catch (e) { return `${e.name}: ${e.message}`; } };
const ws = new WebSocket('http://127.0.0.1:9/socket');
ws.onerror = () => {};
const wsUrl = ws.url;
ws.close();

console.log(JSON.stringify({
  location: location.href === new URL(`file://${process.cwd()}/`).href,
  request: new Request('./api?x=1').url === new URL('./api?x=1', location.href).href,
  rootRequest: new Request('/root').url,
  wsUrl,
  cond, order, orderRequire: require('web-order'), field, map,
  resolved: import.meta.resolve('web-cond').split('/').pop(),
}));
