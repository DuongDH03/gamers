// A stand-in for the world, drawn only in a normal browser (mock bridge),
// so the dev page looks alive behind the menus. Inside WebView2 the backend
// owns the canvas and this never runs: a canvas can have only one kind of
// context, and the backend needs WebGL2.

export function startPlaceholderWorld(canvas: HTMLCanvasElement): () => void {
  const ctx = canvas.getContext('2d');
  if (!ctx) return () => {};
  const still = window.matchMedia?.('(prefers-reduced-motion: reduce)').matches ?? false;
  let frame = 0;

  const resize = () => {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width = Math.round(window.innerWidth * dpr);
    canvas.height = Math.round(window.innerHeight * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    if (still) draw(0); // resizing clears the canvas
  };

  const draw = (ms: number) => {
    const t = still ? 0 : ms / 1000;
    const w = window.innerWidth;
    const h = window.innerHeight;

    const sky = ctx.createLinearGradient(0, 0, 0, h);
    sky.addColorStop(0, '#2b3a55');
    sky.addColorStop(0.55, '#5d7d8f');
    sky.addColorStop(1, '#c9b48a');
    ctx.fillStyle = sky;
    ctx.fillRect(0, 0, w, h);

    // A 2:1 diamond grid drifting slowly, like the isometric view.
    const tileW = 96;
    const tileH = 48;
    const ox = (t * 12) % tileW;
    const oy = (t * 6) % tileH;
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.10)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let x = -h * 2; x < w + h * 2; x += tileW) {
      ctx.moveTo(x + ox, oy - tileH);
      ctx.lineTo(x + ox + h * 2 + tileW, oy + h + tileH);
      ctx.moveTo(x + ox, oy - tileH);
      ctx.lineTo(x + ox - h * 2 - tileW, oy + h + tileH);
    }
    ctx.stroke();

    // Bobbing coins.
    for (let i = 0; i < 9; i += 1) {
      const cx = ((i * 173 + t * 20) % (w + 80)) - 40;
      const cy = h * 0.45 + Math.sin(t * 1.7 + i) * 18 + (i % 3) * h * 0.12;
      const squash = Math.abs(Math.cos(t * 2 + i));
      ctx.fillStyle = '#f2b33d';
      ctx.beginPath();
      ctx.ellipse(cx, cy, 9 * squash + 1, 9, 0, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = 'rgba(0, 0, 0, 0.18)';
      ctx.beginPath();
      ctx.ellipse(cx, cy + 22, 8, 3, 0, 0, Math.PI * 2);
      ctx.fill();
    }

    if (!still) frame = requestAnimationFrame(draw);
  };

  resize();
  window.addEventListener('resize', resize);
  draw(performance.now());
  return () => {
    cancelAnimationFrame(frame);
    window.removeEventListener('resize', resize);
  };
}
