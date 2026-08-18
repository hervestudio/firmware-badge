// Driver headless de l'emulateur (node) : joue une sequence de boutons et
// exporte des captures PPM. Usage : node headless.js <dossier_sortie>
// Boutons : 1 = prev, 2 = next, 4 = centre.
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
    frame(0, 40);  // idle rainbow (avatar par defaut)
    snap("idle_defaut");
    press(4);      // -> home bulles
    frame(0, 30);
    press(2); press(2); press(2); // focus More
    frame(0, 20);
    press(4);      // -> liste More
    frame(0, 10);
    press(2); press(2); press(2); press(2); // -> Settings
    frame(0, 5);
    press(4);      // -> ecran code
    frame(0, 5);
    snap("pin_vide");
    // code 39193
    press(2); press(2); press(2); press(4);           // 3
    press(1); press(4);                               // 9
    press(2); press(4);                               // 1
    press(1); press(4);                               // 9
    press(2); press(2); press(2); press(4);           // 3 -> valide
    frame(0, 10);
    // parcourt les 11 premiers avatars (couvre les 9 visages Figma)
    for (let a = 0; a < (parseInt(process.argv[3]) || 11); a++) {
      frame(0, 8);
      snap(`av_${a}`);
      press(2);
    }
    press(1); press(1); press(1); press(1); press(1);
    press(1); press(1); press(1); press(1); // retour avatar 2 (Kim)
    frame(0, 8);
    press(4);      // save -> menu
    frame(0, 10);
    press(2); press(2); // -> Back
    press(4);      // -> home
    frame(0, 10);
    press(1);      // focus Meet -> ... (retour arriere)
    press(1);      // focus Watch
    press(4);      // -> liste Watch
    frame(0, 5);
    press(4);      // item 0 -> anim idle
    frame(0, 60);  // le temps de regenerer les 11 frames + tourner
    snap("idle_avatar_kim");
    console.log("done");
  },
};
// emu.js (non modularise) attend un `Module` dans sa portee : on l'injecte
// en evaluant le fichier comme une fonction (le `var Module` interne serait
// sinon hoiste et masquerait un global).
const code = fs.readFileSync(require.resolve("./emu.js"), "utf8");
new Function("Module", "require", "__dirname", "__filename",
  code + "\n;return Module;")(EmuModule, require, __dirname, __filename);
