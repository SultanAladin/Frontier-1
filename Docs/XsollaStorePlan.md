# Xsolla Store + Premium + Nitron Economy Plan

Status: research report plus a REST-shaped C++ client (no vendor binaries).
Written 2026-10-07 after the Photon link slice; SDK acquisition filled in the
same day.

## 0. Do you need to download binaries?

**No.** Unlike EOS and Photon, Xsolla is not a closed native `.lib`/`.dll` you
upload to Drive.

| Piece | Status | What we do |
|---|---|---|
| Official C++ Windows SDK zip `com.xsolla.sdk-cpp-{version}.zip` | **Unpublished** ("coming soon" on [xsolla-sdk-windows](https://github.com/xsolla/xsolla-sdk-windows) v3.1.19) | Nothing to download. No headers, no `.lib`, no `.dll`. |
| Official C# zip `com.xsolla.sdk-csharp-3.1.19.zip` | Public, Apache 2.0, ~1.2 MB | Unused. This app is native C++ / MSVC, not .NET. |
| Drive upload | None (folder searched; no Xsolla files) | Do **not** fetch/hash-pin vendor binaries. |
| Merchant API key / merchant id | Server-only | Never put `XSOLLA_API_KEY` or `XSOLLA_MERCHANT_ID` on the game client. |

What **is** the SDK for Frontier: our own C++ client in `Source/XsollaStore.*`
against Shop Builder REST (`https://store.xsolla.com/api`) and Pay Station 4
URLs. Catalog SKUs, sandbox/live checkout URL builder (token never logged),
fail-closed entitlement JSON for the future `ResolvePremiumAccess()` wire.
Manifest: `Build/WindowsXsollaManifest.json` (`files: {}`, `binaries: none`).

What you still do yourself (accounts, not binaries): Publisher Account, create
the project, note the **project ID**, later `XSOLLA_PROJECT_ID`. Sandbox works
before you sign anything.

## TL;DR answers

- **Xsolla as the store: yes, fits.** Catalog, checkout (Pay Station), virtual
  currencies, web-shop/buy-button, and a C++ SDK all exist.
- **Subscriptions: yes, Xsolla has them** (subscription management + recurring
  billing), alongside one-time purchases.
- **Crypto: players can PAY with crypto** (Crypto.com Pay in Pay Station), but
  Xsolla does not issue your own on-chain token. Nitron starts as a
  game-controlled virtual currency; a real blockchain token is a separate,
  much bigger project (see below).
- **Cost: 5% revenue share, no upfront cost.** Xsolla is Merchant of Record
  (tax, PCI, fraud). Real-world total is often 7–10% with channel costs.
- **Premium determination: Xsolla durable item or subscription → webhook to
  your backend → entitlement keyed by Epic account → `ResolvePremiumAccess()`.**

## 1. Setup: what to do in order

1. **Sign up to Publisher Account** (2FA on every login). As an individual,
   use your full name as the business name; pick your country. This also
   starts a draft licensing agreement — fill company/game data correctly now
   to speed up signing later.
2. **Create project**: type Game, name it (English), release platform PC,
   monetization = in-game store + subscriptions, engine custom/native.
   Note the **project ID** next to the project name — everything references it.
3. **Build the catalog** (Store → Items): create the SKUs below (premium pass,
   subscription plan, Nitron currency packs, cosmetic currencies). Prices in
   USD + ZAR at minimum; Xsolla handles regional pricing.
4. **Configure Pay Station**: theme, redirect/return URL, webhook URL
   (your backend endpoint; HTTPS). Enable the payment methods you want,
   including crypto (Crypto.com Pay) if desired.
5. **Test in sandbox**: pass `settings.mode: sandbox` when creating the
   payment token, open `https://sandbox-secure.xsolla.com/paystation4/?token=…`,
   pay with Xsolla's test cards. No real money moves; sandbox works before
   you sign anything.
6. **Sign the licensing agreement** to unlock production payments, then flip
   the token mode to live.

Sources: project setup, sandbox/testing docs (links at the bottom).

## 2. Premium via the store (recommended design)

Keep Epic login exactly as-is. Xsolla never needs to be the identity provider:
every purchase carries **your** user ID (the Epic account ID), and Xsolla's
`user_validation` webhook lets your backend confirm the buyer is registered.

Catalog SKUs for premium:

| SKU | Type | Meaning |
|---|---|---|
| `premium_pass_lifetime` | Durable virtual item, limit 1 per user | Owns premium forever |
| `premium_monthly` | Subscription, monthly recurring | Premium while subscribed |
| `premium_yearly` | Subscription, yearly (discounted) | Premium while subscribed |

Grant flow (all server-to-server; the game client never decides):

1. Player buys in the Xsolla UI (in-game browser overlay or web shop).
2. Xsolla fires **`order_paid`** to your backend with order ID, SKU, user ID.
3. Backend (idempotent on order ID) writes entitlement:
   `epic_account → { premium: lifetime | sub_expires_utc }`.
4. Game client asks your backend "is this Epic account premium?" at login;
   that answer feeds the existing **`ResolvePremiumAccess()`** seam, replacing
   the manual toggle. Subscription expiry/cancellation arrives via webhooks —
   backend flips the flag, next login (or periodic re-check) downgrades.
5. Refunds/chargebacks arrive as **`order_canceled`** — backend revokes.

Notes:

- The official native C++ Windows SDK zip is still unpublished. This client
  talks Shop Builder REST and opens Pay Station by URL; a vendor C++ zip can
  replace the HTTP layer later without changing SKUs or entitlements.
- Until your backend exists, the manual toggle stays. The backend can start
  tiny: one HTTPS endpoint + one table.
- Xsolla Login (their auth product) is **not needed** — Epic remains the login.

## 3. Currency economy: Nitro, Nitron, cosmetics

Recommendation: three tiers. Everything server-authoritative; the client only
displays balances the server sends.

| Currency | Symbol | Source | Spend on |
|---|---|---|---|
| NITRO (soft, earned) | ⚡-like bolt | Race rewards, daily/weekly goals, events | Race entry fees, exchange → cosmetics |
| NITRON (hard, premium) | Hex-coin icon (see `Content/Icons/nitron.png`) | Bought in Nitron packs via Xsolla; small drip from premium sub | Premium races entry, exchange → anything at better rates, exclusive cosmetics |
| MATTER (cosmetic) | — | Exchange from Nitro/Nitron, direct micro-packs | Material unlocks (visual only) |
| PAINT (cosmetic) | — | Exchange from Nitro/Nitron, direct micro-packs | Paints/liveries (visual only) |

Design rules:

- **Cosmetics never affect performance.** Matter/paint buy looks only. Say so
  in every store description — it kills pay-to-win complaints before they start.
- **Race entry is the main sink.** Every race costs Nitro (standard) or Nitron
  (premium/high-stakes); entry fees are deducted server-side when the lobby
  locks, refunded automatically if the race never starts.
- **Nitron packs** (Xsolla virtual-currency items): e.g. Stack 100 / Vault 550
  (+10% bonus) / Reserve 1200 (+20% bonus). Bonus tiers are the standard
  conversion driver.
- **Premium subscribers** get a monthly Nitron drip (e.g. 300) — retention hook.

## 4. Exchange (Nitro → other currencies)

Run the exchange **in-game, server-side**, not in Xsolla. Xsolla sells Nitron
for real money; everything after that is your economy:

- Direction is one-way by default: Nitro/Nitron → Matter/Paint. No cash-out,
  no reverse exchange — this keeps you out of money-transmitter territory.
- Publish fixed rates with a small exchange fee (the fee is a hidden sink that
  fights inflation), e.g. 100 Nitro → 90 Matter after a 10% fee; Nitron
  converts at a flat 1 Nitron = 10 Matter / 10 Paint, no fee (premium perk).
- Rate changes are server config, never a client patch. Log every conversion
  (who, what, rate, timestamp) for support and balancing.
- Show a preview ("you get X") before confirm; confirmations are idempotent.

## 5. Nitron "crypto" reality check

Two very different things share the name "crypto":

1. **Crypto-flavored virtual currency (do this now).** Nitron lives in your
   database + Xsolla inventory, has a coin icon, exchange rates, the works.
   No blockchain, no gas fees, no wallet support tickets, no securities law.
   Players get the fantasy; you keep full control (anti-cheat, chargebacks,
   rebalancing).
2. **Real on-chain token (defer, maybe forever).** Needs a chain choice, audited
   contracts, wallet integration, liquidity, tax/legal opinions per country,
   and makes every economy rebalance a governance event. It also invites bots
   farming cash-outable currency. Nothing in phases 1–3 requires it, and
   nothing precludes adding it later as a Nitron withdrawal bridge.

Verdict: ship (1), revisit (2) only if the game demands withdrawals.

## 6. Anti-fraud rules (non-negotiable)

- Balances, entitlements, exchange, and entry fees are **server-side only**.
- Webhook handler is **idempotent on Xsolla order ID** (Xsolla retries).
- Verify webhook signatures; never trust SKU/price echoed from the client.
- `order_canceled` revokes premium and claws back undelivered currency.
- Rate-limit exchange + entry endpoints; log everything.

## 7. Roadmap (phases)

- **Phase 0 — accounts (you, ~1 day):** Publisher Account, project, sandbox.
- **Phase 1 — catalog + sandbox:** premium SKUs, Nitron packs, test purchase
  end-to-end in sandbox, no game changes.
- **Phase 2 — backend stub:** webhook receiver + entitlement table + premium
  query endpoint; wire `ResolvePremiumAccess()` to it; keep toggle as fallback.
- **Phase 3 — currencies in-game:** balances display, Nitro earn/spend, race
  entry fees, Matter/Paint exchange UI (reuse the lobby-panel patterns).
- **Phase 4 — production:** sign agreement, go live, monitor webhooks.
- **Phase 5 — later:** subscriptions polish, web shop promos, gift cards,
  on-chain bridge only if justified.

## 8. Open decisions (your call)

1. Premium model: lifetime pass, subscription, or both? Suggested prices?
2. Nitron pack sizes and bonus tiers?
3. Monthly Nitron drip amount for subscribers?
4. Exchange rates + fee?
5. Race entry fees (standard vs premium races)?
6. Real on-chain token ever, or virtual-only forever?

## Sources

- Publisher Account + project setup: [1](https://developers.xsolla.com/doc/in-game-store/integration-guide/create-project/), [2](https://developers.xsolla.com/sdk/unity/_archive/v1/integrate-complete-solution/set-up-publisher-project/)
- Virtual items, currencies, prices, limits: [3](https://developers.xsolla.com/items-catalog/items-type/virtual-items/)
- Webhooks (`order_paid`, `order_canceled`, `user_validation`) + granting: [4](https://developers.xsolla.com/solutions/web-shop/catalog-and-items/grant-purchases/), [5](https://developers.xsolla.com/sdk/mobile/windowstores/sdk/validation/)
- C++ purchase-validation SDK: [5](https://developers.xsolla.com/sdk/mobile/windowstores/sdk/validation/)
- Subscriptions + recurring billing: [6](https://noda.live/articles/xsolla-vs-stripe), [7](https://xsolla.com/newsroom/empowering-developers-to-monetize-anywhere-xsolla-expands-platform-support-for-cross-platform-direct-to-consumer-commerce)
- Crypto payments via Crypto.com Pay: [8](https://crypto.com/en/company-news/xsolla-and-crypto-com-partner-to-integrate-payment-solutions)
- 5% revenue share, Merchant of Record: [9](https://toolradar.com/tools/xsolla)
- Sandbox mode + test cards: [10](https://developers.xsolla.com/doc/pay-station/testing/general-info/), [11](https://developers.xsolla.com/dev-resources/testing/sandbox-mode/test-cards-in-sandbox/)
- Windows SDK repo (C# zip public, C++ zip "coming soon"): [12](https://github.com/xsolla/xsolla-sdk-windows)
- Shop Builder create-order + Pay Station token: [13](https://developers.xsolla.com/api/shop-builder/operation/create-order-with-item/)
