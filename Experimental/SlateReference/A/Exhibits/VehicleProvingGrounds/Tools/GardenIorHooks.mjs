import assert from 'node:assert/strict';
import { CourseMaterial, IorToF0 } from '../Source/MaterialSpecification.js';
export default {
    BeforeFrame(Host, Frame) {
        // 📝 Drive the garden-glass IOR slider mid-run: two re-uploads of the one record through Dawn validation.
        if (Frame === 4 || Frame === 8) {
            Host.Settings.GlassIor = Frame === 4 ? 2.0 : 1.33;
            Host.ApplySetting('GlassIor');
            assert.ok(Math.abs(Host.Materials[CourseMaterial.ShowcaseGlass].SpecularLevel - IorToF0(Host.Settings.GlassIor)) < 1.0e-6);
            assert.equal(Host.PriorValid, false);
        }
    },
    Report(Host) {
        assert.equal(Host.Settings.GlassIor, 1.33);
        console.log('garden glass IOR re-uploads exercised (2.0, then water 1.33)');
    }
};
