import { Hono } from 'hono';
import { serve, upgradeWebSocket } from '@hono/ant';

const app = new Hono();

app.get('/', c => c.text('hello hono websocket\n'));

app.get(
  '/ws',
  upgradeWebSocket(() => ({
    onMessage(event, ws) {
      ws.send(event.data);
    }
  }))
);

export default {
  port: 3000,
  fetch: app.fetch
};
