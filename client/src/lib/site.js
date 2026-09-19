// Supply real operator details at build time. Never infer them from a hostname.
const raw = import.meta.env.VITE_SITE_URL || '';
export const siteUrl = (() => { try { const u = new URL(raw); return u.protocol === 'https:' ? u.origin : ''; } catch { return ''; } })();
export const operator = import.meta.env.VITE_OPERATOR_NAME || '';
export const country = import.meta.env.VITE_OPERATOR_COUNTRY || '';
export const contact = import.meta.env.VITE_CONTACT_EMAIL || '';
