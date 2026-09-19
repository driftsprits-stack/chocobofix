import { useEffect } from 'react';
import { useLocation } from 'react-router-dom';
import { siteUrl } from '../lib/site.js';
const meta = {
 '/':['Railway access planning', 'Plan railway access, compare scenarios and coordinate work with ChocoboFix.'],
 '/overview':['Today', 'Your projects and planned railway access for the current week.'],
 '/projects':['Projects', 'Manage railway access projects and source files.'],
 '/schedules':['Schedules', 'View planned activities by contract, coordinator and week.'],
 '/settings':['Settings', 'Set your appearance, text size, motion and profile photo.'],
 '/signin':['Sign in', 'Sign in to your ChocoboFix workspace.'],
 '/terms':['Terms', 'Terms for use of the ChocoboFix planning workspace.'],
 '/privacy':['Privacy', 'How the ChocoboFix workspace stores and uses information.'],
 '/cookies':['Cookies and storage', 'Browser storage used by ChocoboFix.'],
 '/sample':['Public sample', 'Generate schedules from the public PS1 sample dataset.']
};
export default function PageMeta() {
 const {pathname} = useLocation();
 useEffect(() => {
  window.scrollTo(0,0);
  const [title,description] = meta[pathname] || (pathname.startsWith('/projects/') ? ['Project details','Review project files and generated access plans.'] : ['Page not found','The requested page could not be found.']);
  document.title = `${title} / ChocoboFix`;
  const put = (selector, attrs) => { let e=document.head.querySelector(selector); if(!e) { e=document.createElement(selector.startsWith('link')?'link':'meta'); document.head.append(e); } Object.entries(attrs).forEach(([k,v])=>e.setAttribute(k,v)); };
  put('meta[name="description"]',{name:'description',content:description});
  put('meta[property="og:title"]',{property:'og:title',content:document.title});
  put('meta[property="og:description"]',{property:'og:description',content:description});
  if(siteUrl) {
   put('link[rel="canonical"]',{rel:'canonical',href:siteUrl+pathname});
   put('meta[property="og:url"]',{property:'og:url',content:siteUrl+pathname});
   put('meta[property="og:image"]',{property:'og:image',content:siteUrl+'/social-preview.png'});
  }
 },[pathname]);
 return null;
}
