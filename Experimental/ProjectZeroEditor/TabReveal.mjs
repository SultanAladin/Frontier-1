// For the checks: the lettered inspector pages (InspectorTabs.jsx) and the Showcase categories are picked from the Orbit menu
// (OrbitMenu.jsx), whose page buttons exist only while its ring is open.

// Opens the Orbit ring (of the menu holding Owner's pages, or the only one) if it is shut.
export async function OpenOrbit(Page, Owner = null) {
  const Button = Page.locator(`${Owner ? `[data-orbit="${Owner}"]` : ".orbit-menu"} .orbit-button[aria-expanded="false"]`).first();
  if (await Button.count()) await Button.click();
  await Page.locator(`${Owner ? `[data-orbit="${Owner}"]` : ".orbit-menu"} .orbit-ring`).first().waitFor();
}
// Turns to a lettered page ("owner:page"), opening the ring first.
export async function PickTab(Page, Tab) {
  if (await Page.locator(`[data-tab-page="${Tab}"]:not([hidden])`).count()) return;
  await OpenOrbit(Page, Tab.split(":")[0]);
  await Page.locator(`[data-tab-for="${Tab}"]`).click();
  await Page.locator(`[data-tab-page="${Tab}"]`).waitFor();
}
// A Showcase category button (by name), with the ring open.
export async function CategoryPill(Page, Name) {
  await OpenOrbit(Page, "Category");
  return Page.locator(`.pi-pill[aria-label="${Name}"]`);
}
// Turns to a Showcase category.
export async function PickCategory(Page, Name) {
  await (await CategoryPill(Page, Name)).click();
  await Page.locator(`.pi-page[aria-label="${Name}"]`).waitFor();
}

// Opens the lettered page holding a control, when that page is shut.
export async function OpenTabOf(Target) {
  const Tab = await Target.evaluate((Each) => {
    const Holder = Each.closest("[data-tab-page]");
    return Holder?.hidden ? Holder.dataset.tabPage : null;
  });
  if (Tab) await PickTab(Target.page(), Tab);
}
// The same for a control found by its accessible label or a card by its title (role locators skip shut pages, so they can't be used).
export async function OpenTabWith(Page, Label) {
  const Tab = await Page.evaluate((Label) => {
    const Hit = [...document.querySelectorAll("[data-tab-page] [aria-label], [data-tab-page] [data-card]")].find(
      (Each) => Each.getAttribute("aria-label") === Label || Each.dataset.card === Label,
    );
    const Holder = Hit?.closest("[data-tab-page]");
    return Holder?.hidden ? Holder.dataset.tabPage : null;
  }, Label);
  if (Tab) await PickTab(Page, Tab);
}
