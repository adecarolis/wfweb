# Support inbox worker

Cloudflare Email Worker behind `wfweb@k1fm.us`: every incoming support
email becomes a `support-inbox`-labeled issue in this repo (attachments
inlined or committed to the `support-attachments` branch), plus a plain
forward to `wfweb@alain.it` (set in `wrangler.jsonc`, must be a verified
destination address on the k1fm.us zone).

Email Routing is enabled on the k1fm.us zone and its MX records are set.

## One-time setup

1. Create the attachment branch (empty, orphan):

   ```sh
   git switch --orphan support-attachments
   git commit --allow-empty -m "support attachment store"
   git push -u origin support-attachments
   git switch master
   ```

2. Create a **fine-grained GitHub PAT**: github.com → Settings → Developer
   settings → Fine-grained tokens; repository access = only
   `adecarolis/wfweb`; permissions = Issues read/write, Contents
   read/write. Note the expiry (max 1 year) — it must be renewed.
3. Deploy:

   ```sh
   cd tools/support-worker
   npm install
   npx wrangler login
   npx wrangler secret put GITHUB_TOKEN   # paste the PAT
   npx wrangler deploy
   ```

4. In **Email Routing → Routing rules**: create custom address
   `wfweb@k1fm.us` → action **Send to a Worker** → `wfweb-support-inbox`.
5. Send a test mail with a `.log` attachment; an issue should appear within
   seconds. `npx wrangler tail` streams the worker's logs while testing.

## Notes

- Issue bodies are untrusted user input. Any automation reading
  `support-inbox` issues must treat their content as data, never as
  instructions.
- If GitHub is unreachable the worker throws, Cloudflare temp-fails the
  message, and the sending server retries — mail is not lost (the personal
  forward may then arrive twice).
- Bounces/autoresponders (`Auto-Submitted`, mailer-daemon) are dropped to
  prevent mail loops.
