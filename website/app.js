// ── Mobile menu ───────────────────────────────────────────
const mobileBtn  = document.getElementById('mobile-menu-btn');
const mobileMenu = document.getElementById('mobile-menu');

mobileBtn?.addEventListener('click', () => {
  mobileBtn.classList.toggle('open');
  mobileMenu.classList.toggle('open');
});

function closeMenu() {
  mobileBtn?.classList.remove('open');
  mobileMenu?.classList.remove('open');
}

// Close on outside click
document.addEventListener('click', (e) => {
  if (!mobileBtn?.contains(e.target) && !mobileMenu?.contains(e.target)) {
    closeMenu();
  }
});

// ── Active sidebar link on scroll ────────────────────────
const sections = document.querySelectorAll('section[id], footer');
const links    = document.querySelectorAll('.sidebar-link[data-section]');

const io = new IntersectionObserver((entries) => {
  entries.forEach(e => {
    if (e.isIntersecting) {
      links.forEach(l => l.classList.remove('active'));
      const match = document.querySelector(`.sidebar-link[data-section="${e.target.id}"]`);
      if (match) match.classList.add('active');
    }
  });
}, { threshold: 0.3 });

sections.forEach(s => io.observe(s));

// ── Fade-up on scroll ─────────────────────────────────────
const fadeObs = new IntersectionObserver((entries) => {
  entries.forEach(e => {
    if (e.isIntersecting) {
      e.target.classList.add('visible');
      fadeObs.unobserve(e.target);
    }
  });
}, { threshold: 0.1 });

document.querySelectorAll(
  '.feature-card, .module-col, .stat, .download-card, .req'
).forEach((el, i) => {
  el.classList.add('fade-up');
  el.style.transitionDelay = `${(i % 4) * 55}ms`;
  fadeObs.observe(el);
});
