import { test, expect } from '@playwright/test';
test('shared tooltip uses opaque active-theme colours, pointer, shortcut and keyboard dismissal', async ({ page: Page }) =>
{
  const Failures = []; Page.on('pageerror', Failure => Failures.push(Failure.message));
  await Page.goto('/?studio=pattern&pattern=Blank');
  const Button = Page.getByRole('button', { name: 'Rectangle tool (R)', exact: true });
  await Button.evaluate(Element =>
  {
    Element.style.setProperty('--panel', '#faf6ec'); Element.style.setProperty('--muted', '#53504a'); Element.style.setProperty('--border', '#d4cbb9'); Element.style.setProperty('--accent', '#3c735e'); Element.style.color = '#24221e';
    Element.setAttribute('aria-describedby', 'existing-tool-description');
  });
  await Button.hover();
  const Tip = Page.getByRole('tooltip');
  await expect(Tip).toBeVisible();
  await expect(Tip).toContainText('Rectangle tool'); await expect(Tip).toContainText('current fill and stroke');
  await expect(Tip.locator('kbd')).toHaveText('R');
  await expect(Tip).toHaveCSS('background-color', 'rgb(250, 246, 236)');
  await expect(Tip).toHaveCSS('color', 'rgb(36, 34, 30)'); await expect(Tip).toHaveCSS('border-radius', '18px');
  await expect(Tip).toHaveAttribute('data-placement', 'right');
  await expect(Button).toHaveAttribute('aria-describedby', 'existing-tool-description studio-tool-description');
  const Bounds = await Tip.boundingBox(); expect(Bounds.x).toBeGreaterThanOrEqual(12); expect(Bounds.y).toBeGreaterThanOrEqual(12);
  await Page.keyboard.press('Escape'); await expect(Tip).toHaveCount(0);
  await expect(Button).toHaveAttribute('aria-describedby', 'existing-tool-description');
  await expect(Button).toHaveAttribute('title', 'Rectangle tool (R)');
  await Page.mouse.move(1, 1); await Page.keyboard.press('Tab'); await Button.focus();
  await expect(Tip).toBeVisible();
  await Page.keyboard.press('Escape'); await expect(Tip).toHaveCount(0);
  expect(Failures).toEqual([]);
});
test('shared tooltip flips at viewport edges, ignores touch and follows workspace changes', async ({ page: Page }) =>
{
  await Page.goto('/?studio=pattern&pattern=Blank');
  await Page.evaluate(() =>
  {
    const Element = document.createElement('button'); Element.setAttribute('aria-label', 'Edge tooltip'); Element.dataset.tooltipTitle = 'Edge tooltip'; Element.dataset.tooltipBody = 'A shared tooltip remains inside the viewport.';
    Element.style.position = 'fixed'; Element.style.right = '12px'; Element.style.top = '12px'; Element.style.zIndex = '9999'; Element.style.width = '30px'; Element.style.height = '30px'; Element.dataset.tooltipPlacement = 'right'; document.body.append(Element);
  });
  const Button = Page.getByRole('button', { name: 'Edge tooltip', exact: true });
  await Button.hover();
  const Tip = Page.getByRole('tooltip'); await expect(Tip).toBeVisible();
  const Bounds = await Tip.boundingBox(); expect(Bounds.x + Bounds.width).toBeLessThanOrEqual(1428);
  await expect(Tip).not.toHaveAttribute('data-placement', 'right');
  await Page.keyboard.press('Escape'); await Page.mouse.move(1, 1);
  await Button.dispatchEvent('pointerover', { pointerType: 'touch' });
  await expect(Tip).toHaveCount(0);
  await Page.goto('/?studio=texture');
  const Add = Page.getByRole('button', { name: 'Add layer', exact: true }); await Add.hover();
  await expect(Tip).toBeVisible(); await expect(Tip).toContainText('Add layer');
  await Add.click(); await expect(Tip).toHaveCount(0); await expect(Page.getByRole('menu', { name: 'Add texture layer' })).toBeVisible();
  await Page.keyboard.press('ArrowDown'); await expect(Tip).toBeVisible();
  await Page.keyboard.press('Escape');
  await expect(Page.getByRole('menu', { name: 'Add texture layer' })).toHaveCount(0);
  await expect(Add).toBeFocused();
});
