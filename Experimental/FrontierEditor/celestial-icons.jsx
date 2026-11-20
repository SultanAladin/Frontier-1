import React, {forwardRef} from 'react';

// A small, optically matched 24px family. currentColor preserves each entity's
// accent; translucent fills give larger inspector icons depth without gradients.
const frame = (name, draw) => {
  const Icon = forwardRef(function CelestialIcon({size=24,strokeWidth=1.5,className='',title,...props},ref) {
    const labelled=Boolean(title||props['aria-label']||props['aria-labelledby']);
    return <svg ref={ref} xmlns="http://www.w3.org/2000/svg" width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={strokeWidth} strokeLinecap="round" strokeLinejoin="round" className={`celestial-icon celestial-icon--${name} ${className}`.trim()} aria-hidden={labelled?undefined:true} role={labelled?'img':undefined} focusable="false" {...props}>{title&&<title>{title}</title>}{draw}</svg>;
  });
  Icon.displayName=name[0].toUpperCase()+name.slice(1)+'Icon';
  return Icon;
};

export const Sun = frame('sun', <>
  <circle cx="12" cy="12" r="4.35" fill="currentColor" fillOpacity=".13"/>
  <path d="M12 2v2.2m0 15.6V22M2 12h2.2m15.6 0H22"/>
  <path d="m4.93 4.93 1.5 1.5m11.14 11.14 1.5 1.5m0-14.14-1.5 1.5M6.43 17.57l-1.5 1.5" strokeOpacity=".8"/>
  <path d="M9.6 11.5a2.45 2.45 0 0 1 2.05-1.94" strokeWidth="1" strokeOpacity=".55"/>
</>);

export const Moon = frame('moon', <>
  <path d="M20.65 13.42A8.85 8.85 0 1 1 10.58 3.3a6.85 6.85 0 0 0 10.07 10.12Z" fill="currentColor" fillOpacity=".12"/>
  <circle cx="7.2" cy="13.1" r="1.15" strokeWidth=".8" strokeOpacity=".5"/>
  <path d="M9.6 17.65a2.9 2.9 0 0 0 2.5.63" strokeWidth=".9" strokeOpacity=".45"/>
  <path d="M17.15 3.1v3.5M15.4 4.85h3.5" strokeWidth="1.1" strokeOpacity=".8"/>
</>);

export const Stars = frame('stars', <>
  <path d="M8.6 7.75c.8 4.05 1.7 4.95 5.75 5.75-4.05.8-4.95 1.7-5.75 5.75-.8-4.05-1.7-4.95-5.75-5.75 4.05-.8 4.95-1.7 5.75-5.75Z" fill="currentColor" fillOpacity=".14"/>
  <path d="M17.6 2.6c.43 2.53 1.02 3.12 3.55 3.55-2.53.43-3.12 1.02-3.55 3.55-.43-2.53-1.02-3.12-3.55-3.55 2.53-.43 3.12-1.02 3.55-3.55Z" fill="currentColor" fillOpacity=".24" strokeWidth="1.15"/>
  <path d="M18.8 15.7v4.2m-2.1-2.1h4.2" strokeWidth="1.2" strokeOpacity=".7"/>
  <circle cx="5.5" cy="4.5" r=".8" fill="currentColor" stroke="none" opacity=".6"/>
</>);
