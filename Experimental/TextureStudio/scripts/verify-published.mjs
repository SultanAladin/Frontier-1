// Verify the actual published artifact, not just that some URL returns HTTP 200.
// Requires the user's existing gh authentication; never handles credentials.
import { execFileSync } from "node:child_process";
import { readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { fileURLToPath } from "node:url";

const cwd = fileURLToPath(new URL("../", import.meta.url));
const run = (command, args) =>
  execFileSync(command, args, {
    cwd,
    encoding: "utf8",
    maxBuffer: 20 * 1024 * 1024,
  });
const gh = (endpoint) => JSON.parse(run("gh", ["api", endpoint]));
const hash = (bytes) => createHash("sha256").update(bytes).digest("hex");
const file = "site/index.html";

try {
  const repository = JSON.parse(
    run("gh", ["repo", "view", "--json", "nameWithOwner,isPrivate"]),
  );
  if (repository.isPrivate)
    throw new Error(
      "GitHack requires a publicly accessible artifact; this repository is private.",
    );
  // Source/test-only commits don't need a new CDN URL. Use the commit which
  // last changed the generated page, and ensure local bytes still match it.
  const commit = run("git", ["log", "-1", "--format=%H", "--", file]).trim();
  if (!commit)
    throw new Error("Commit site/index.html before verifying publication.");
  const blobSHA = run("git", ["hash-object", file]).trim();
  const repo = repository.nameWithOwner;
  const metadata = gh(`repos/${repo}/contents/${file}?ref=${commit}`);
  if (metadata.sha !== blobSHA)
    throw new Error(
      "The local page differs from the published artifact. Rebuild, commit and push before verifying.",
    );
  // Decode the Git blob's base64 payload to avoid text/escape transformations
  // in API transport wrappers. Compare the complete file bytes.
  const blob = gh(`repos/${repo}/git/blobs/${metadata.sha}`);
  if (blob.encoding !== "base64")
    throw new Error(`Unexpected Git blob encoding: ${blob.encoding}`);
  const remote = Buffer.from(blob.content, "base64");
  const local = await readFile(new URL(`../${file}`, import.meta.url));
  if (remote.length !== metadata.size || !remote.equals(local))
    throw new Error("Published artifact failed the byte-for-byte comparison.");
  console.log(
    `Verified ${remote.length.toLocaleString("en-US")} published bytes.`,
  );
  console.log(`SHA-256: ${hash(remote)}`);
  console.log(`Open: https://raw.githack.com/${repo}/${commit}/${file}`);
  console.log(
    "GitHack may ask you to select ‘Open the page’. This verifies the GitHub artifact, not GitHack's live response.",
  );
} catch (error) {
  console.error(`Publication verification failed: ${error.message}`);
  process.exitCode = 1;
}
