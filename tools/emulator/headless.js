// Headless driver for the emulator (node): plays a button sequence and
// exports PPM captures. Usage: node headless.js <output_dir>
// Buttons: 1 = prev, 2 = next, 4 = center.
const fs = require("fs");
const out = process.argv[2] || ".";

const EmuModule = {
  onRuntimeInitialized: () => {
    const M = EmuModule;
    M._emu_init();
    let shot = 0;

    const frame = (mask, n) => {
      for (let i = 0; i < n; i++) M._emu_frame(50, mask);
    };
    const press = (mask) => {
      frame(mask, 3);
      frame(0, 3);
    };
    const snap = (name) => {
      const p = M._emu_fb() >> 1;
      const fb = M.HEAPU16.subarray(p, p + 360 * 360);
      const buf = Buffer.alloc(360 * 360 * 3);
      for (let i = 0; i < 360 * 360; i++) {
        const c = fb[i];
        buf[i * 3] = ((c >> 11) & 31) << 3;
        buf[i * 3 + 1] = ((c >> 5) & 63) << 2;
        buf[i * 3 + 2] = (c & 31) << 3;
      }
      fs.writeFileSync(`${out}/${String(++shot).padStart(2, "0")}_${name}.ppm`,
        Buffer.concat([Buffer.from(`P6\n360 360\n255\n`), buf]));
      console.log("snap", name);
    };

    frame(0, 90);  // boot splash 4 s
    frame(0, 40);  // idle rainbow (default avatar)
    snap("idle_defaut");
    press(4);      // -> home bubbles
    frame(0, 30);
    press(2); press(2); press(2); // focus More
    frame(0, 20);
    press(4);      // -> More list
    frame(0, 10);
    press(2); press(2); press(2); press(2); // -> Settings
    frame(0, 5);
    press(4);      // -> PIN code screen
    frame(0, 5);
    snap("pin_vide");
    // code 39193
    press(2); press(2); press(2); press(4);           // 3
    press(1); press(4);                               // 9
    press(2); press(4);                               // 1
    press(1); press(4);                               // 9
    press(2); press(2); press(2); press(4);           // 3 -> submit
    frame(0, 10);
    // walk the first 11 avatars (covers the 9 Figma faces)
    for (let a = 0; a < (parseInt(process.argv[3]) || 11); a++) {
      frame(0, 8);
      snap(`av_${a}`);
      press(2);
    }
    press(1); press(1); press(1); press(1); press(1);
    press(1); press(1); press(1); press(1); // back to avatar 2 (Kim)
    frame(0, 8);
    press(4);      // save -> menu
    frame(0, 10);
    press(2); press(2); // -> Back
    press(4);      // -> home
    frame(0, 10);
    press(1);      // focus Meet -> ... (stepping back)
    press(1);      // focus Watch
    press(4);      // -> Watch list
    frame(0, 5);
    press(4);      // item 0 -> idle anim
    frame(0, 60);  // time to regenerate the 11 frames + spin
    snap("idle_avatar_kim");
    console.log("done");
  },
};
// emu.js (non-modularized) expects a `Module` in its scope: inject it by
// evaluating the file as a function (the internal `var Module` would
// otherwise be hoisted and shadow a global).
const code = fs.readFileSync(require.resolve("./emu.js"), "utf8");
new Function("Module", "require", "__dirname", "__filename",
  code + "\n;return Module;")(EmuModule, require, __dirname, __filename);
