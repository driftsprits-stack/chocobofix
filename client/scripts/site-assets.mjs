import { writeFileSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { loadEnv } from 'vite';
const env={...loadEnv('production',process.cwd(),''),...process.env};
let origin='';
if(env.VITE_SITE_URL) { const u=new URL(env.VITE_SITE_URL); if(u.protocol!=='https:') throw Error('VITE_SITE_URL must use HTTPS'); origin=u.origin; }
const escape=s=>s.replaceAll('&','&amp;').replaceAll('"','&quot;').replaceAll('<','&lt;').replaceAll('>','&gt;');
const root=resolve('../web-dist');
const paths=['/','/terms','/privacy','/cookies'];
writeFileSync(resolve(root,'robots.txt'),`User-agent: *\nDisallow: /api/\n${origin ? `Sitemap: ${origin}/sitemap.xml\n` : ''}`);
writeFileSync(resolve(root,'sitemap.xml'),`<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">${origin ? paths.map(p=>`<url><loc>${escape(origin+p)}</loc></url>`).join('') : ''}</urlset>\n`);
if(origin) {
 const structured={ '@context':'https://schema.org','@type':'WebApplication',name:'ChocoboFix',url:origin,applicationCategory:'BusinessApplication',operatingSystem:'Web browser' };
 const tags=`<link rel="canonical" href="${escape(origin)}/"><meta property="og:url" content="${escape(origin)}/"><meta property="og:image" content="${escape(origin)}/social-preview.png"><script type="application/ld+json">${JSON.stringify(structured).replaceAll('<','\\u003c')}</script>`;
 const f=resolve(root,'index.html');writeFileSync(f,readFileSync(f,'utf8').replace('</head>',tags+'</head>'));
} else console.log('Public domain is unset: canonical URLs and sitemap entries remain unconfigured.');
