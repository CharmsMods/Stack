/* 
  STACK Website Prototype 3 - Interactive Logic
  Aesthetics: Synapser Studio
*/

// ================== 1. TEXT SCRAMBLER EFFECT ==================
class TextScrambler {
  constructor(el) {
    this.el = el;
    this.chars = '!<>-_\\/[]{}—=+*^?#________';
    this.update = this.update.bind(this);
  }

  setText(newText) {
    const oldText = this.el.innerText;
    const length = Math.max(oldText.length, newText.length);
    const promise = new Promise((resolve) => this.resolve = resolve);
    this.queue = [];
    for (let i = 0; i < length; i++) {
      const from = oldText[i] || '';
      const to = newText[i] || '';
      const start = Math.floor(Math.random() * 20);
      const end = start + Math.floor(Math.random() * 20);
      this.queue.push({ from, to, start, end });
    }
    cancelAnimationFrame(this.frameRequest);
    this.frame = 0;
    this.update();
    return promise;
  }

  update() {
    let output = '';
    let complete = 0;
    for (let i = 0, n = this.queue.length; i < n; i++) {
      let { from, to, start, end, char } = this.queue[i];
      if (this.frame >= end) {
        complete++;
        output += to;
      } else if (this.frame >= start) {
        if (!char || Math.random() < 0.28) {
          char = this.randomChar();
          this.queue[i].char = char;
        }
        output += `<span style="opacity: 0.6; font-family: monospace;">${char}</span>`;
      } else {
        output += from;
      }
    }
    this.el.innerHTML = output;
    if (complete === this.queue.length) {
      this.resolve();
    } else {
      this.frameRequest = requestAnimationFrame(this.update);
      this.frame++;
    }
  }

  randomChar() {
    return this.chars[Math.floor(Math.random() * this.chars.length)];
  }
}

// Bind scramble effects to elements
function initScramblers() {
  document.querySelectorAll('[data-scramble]').forEach(el => {
    const originalText = el.innerText;
    const scrambler = new TextScrambler(el);
    let isScrambling = false;

    el.addEventListener('mouseenter', () => {
      if (isScrambling) return;
      isScrambling = true;
      scrambler.setText(originalText).then(() => {
        isScrambling = false;
      });
    });
  });
}


/* Physics canvas code removed */


// ================== 3. CHROMATIC ABERRATION DYNAMIC MOUSE-TRACKER ==================
// Removed chromatic aberration mouse-tracker script


// ================== 4. SMOOTH SCROLL INTERPOLATOR ==================
class SmoothScroll {
  constructor() {
    this.targetY = window.scrollY;
    this.currentY = window.scrollY;
    this.ease = 0.08;
    this.isScrolling = false;

    // Only enable smooth scroll on non-touch desktop devices for premium feel
    if (!('ontouchstart' in window) && window.innerWidth > 768) {
      this.init();
    }
  }

  init() {
    document.body.style.height = 'auto';
    
    // Listen to wheel events
    window.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.targetY += e.deltaY * 0.85;
      this.targetY = Math.max(0, Math.min(this.targetY, document.documentElement.scrollHeight - window.innerHeight));
      
      if (!this.isScrolling) {
        this.isScrolling = true;
        this.animate();
      }
    }, { passive: false });

    // Handle internal link navigation scrolls
    document.querySelectorAll('a[href^="#"]').forEach(anchor => {
      anchor.addEventListener('click', (e) => {
        e.preventDefault();
        const targetId = anchor.getAttribute('href');
        const targetEl = document.querySelector(targetId);
        if (targetEl) {
          this.targetY = targetEl.getBoundingClientRect().top + window.scrollY;
          if (!this.isScrolling) {
            this.isScrolling = true;
            this.animate();
          }
        }
      });
    });

    // Reset offsets on native scroll event (e.g. keypress paging)
    window.addEventListener('scroll', () => {
      if (!this.isScrolling) {
        this.currentY = window.scrollY;
        this.targetY = window.scrollY;
      }
    });
  }

  animate() {
    const diff = this.targetY - this.currentY;
    if (Math.abs(diff) > 0.5) {
      this.currentY += diff * this.ease;
      window.scrollTo(0, this.currentY);
      requestAnimationFrame(() => this.animate());
    } else {
      this.currentY = this.targetY;
      window.scrollTo(0, this.currentY);
      this.isScrolling = false;
    }
  }
}


// ================== 5. GITHUB RELEASE LOADER ==================
const GITHUB_OWNER = 'CharmsMods';
const GITHUB_REPO = 'Stack';
const INSTALLER_PATTERN = /^stacksetup-v?\d+\.\d+\.\d+-win-x64\.exe$/i;
const PORTABLE_PATTERN = /^stack-v?\d+\.\d+\.\d+-win-x64\.zip$/i;

function scoreInstaller(assetName) {
  const name = assetName.toLowerCase();
  if (!name.endsWith('.exe')) return -1000;
  let score = 0;
  if (INSTALLER_PATTERN.test(assetName)) score += 100;
  if (name.includes('stacksetup')) score += 50;
  if (name.includes('win') || name.includes('windows')) score += 20;
  if (name.includes('x64')) score += 10;
  if (name.includes('portable') || name.includes('source')) score -= 60;
  return score;
}

function scorePortable(assetName) {
  const name = assetName.toLowerCase();
  if (!name.endsWith('.zip')) return -1000;
  let score = 0;
  if (PORTABLE_PATTERN.test(assetName)) score += 100;
  if (name.includes('stack')) score += 30;
  if (name.includes('win') || name.includes('windows')) score += 20;
  if (name.includes('x64')) score += 10;
  if (name.includes('source') || name.includes('src')) score -= 80;
  return score;
}

function pickAsset(release, scorer) {
  if (!release || !Array.isArray(release.assets)) return null;
  let bestAsset = null;
  let bestScore = -1000;
  for (const asset of release.assets) {
    const score = scorer(asset.name || '');
    if (score > bestScore) {
      bestScore = score;
      bestAsset = asset;
    }
  }
  return bestScore >= 0 ? bestAsset : null;
}

async function loadLatestRelease() {
  const downloadLink = document.getElementById('downloadLink');
  const releaseMeta = document.getElementById('releaseMeta');
  if (!downloadLink) return;

  try {
    const response = await fetch(`https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}/releases/latest`, {
      headers: { 'Accept': 'application/vnd.github+json' }
    });

    if (!response.ok) throw new Error(`GitHub returned ${response.status}`);

    const release = await response.json();

    if (release && !release.draft && !release.prerelease) {
      const installerAsset = pickAsset(release, scoreInstaller);
      const portableAsset = pickAsset(release, scorePortable);
      if (installerAsset && installerAsset.browser_download_url) {
        downloadLink.href = installerAsset.browser_download_url;
      }
      
      if (releaseMeta) {
        const date = new Date(release.published_at).toLocaleDateString(undefined, {
          year: 'numeric',
          month: 'short',
          day: 'numeric'
        });
        const portableSuffix = portableAsset && portableAsset.browser_download_url
          ? ' // PORTABLE ZIP ALSO AVAILABLE'
          : '';
        releaseMeta.innerHTML = `LATEST RELEASE: <a href="${release.html_url}" target="_blank">${release.name}</a> (${date}) // WINDOWS INSTALLER${portableSuffix}`;
      }
    }
  } catch (error) {
    console.error('Error fetching release from Github:', error);
    // fallback is preserved in default HTML links
  }
}


// ================== 6. REAL-TIME PER-PIXEL NOISE GENERATOR ==================
class PixelNoise {
  constructor(canvasId) {
    this.canvas = document.getElementById(canvasId);
    if (!this.canvas) return;

    this.ctx = this.canvas.getContext('2d');
    this.tiles = [];
    this.frameCount = 6;
    this.currentFrame = 0;
    this.tick = 0;

    this.init();
  }

  init() {
    this.generateTiles();
    this.resize();
    window.addEventListener('resize', () => this.resize());
    this.animate();
  }

  resize() {
    const dpr = window.devicePixelRatio || 1;
    this.canvas.width = window.innerWidth * dpr;
    this.canvas.height = window.innerHeight * dpr;
  }

  generateTiles() {
    const tileSize = 256;
    for (let f = 0; f < this.frameCount; f++) {
      const tileCanvas = document.createElement('canvas');
      tileCanvas.width = tileSize;
      tileCanvas.height = tileSize;
      const tileCtx = tileCanvas.getContext('2d');
      const imgData = tileCtx.createImageData(tileSize, tileSize);
      const data = imgData.data;

      // Generate organic sparse noise (only ~12% density)
      for (let i = 0; i < data.length; i += 4) {
        if (Math.random() < 0.12) {
          const val = Math.floor(Math.random() * 50); // dark grain spots
          data[i] = val;     // R
          data[i+1] = val;   // G
          data[i+2] = val;   // B
          data[i+3] = Math.floor(Math.random() * 150) + 60; // varying alpha opacity
        } else {
          data[i+3] = 0;     // transparent background
        }
      }
      tileCtx.putImageData(imgData, 0, 0);
      this.tiles.push(tileCanvas);
    }
  }

  animate() {
    this.tick++;
    // Throttle frame change to 10fps (every 6 render ticks) for distinct film flicker
    if (this.tick % 6 === 0) {
      this.currentFrame = (this.currentFrame + 1) % this.frameCount;
      
      this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
      
      this.ctx.save();
      // Apply a random translation offset to avoid grid repetition patterns
      const tx = Math.floor(Math.random() * 256);
      const ty = Math.floor(Math.random() * 256);
      this.ctx.translate(tx, ty);
      
      const pattern = this.ctx.createPattern(this.tiles[this.currentFrame], 'repeat');
      this.ctx.fillStyle = pattern;
      
      // Draw over full canvas bounds including translation offset space
      this.ctx.fillRect(-tx, -ty, this.canvas.width, this.canvas.height);
      this.ctx.restore();
    }

    requestAnimationFrame(() => this.animate());
  }
}


// ================== INITIALIZATION ==================
document.addEventListener('DOMContentLoaded', () => {
  // Init real-time per-pixel noise canvas
  new PixelNoise('noiseCanvas');

  // Init scrambler text hover triggers
  initScramblers();

  // Init smooth scroll
  new SmoothScroll();

  // Load latest GitHub releases
  loadLatestRelease();
});
