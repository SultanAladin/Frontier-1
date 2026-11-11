import React, { useState, useRef } from 'react';


const DocumentWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="docFront" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="badgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <filter id="docShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
    </defs>

    <g transform="translate(120, 120)">
      {/* Left Back Document */}
      <g transform="translate(-55, -45) rotate(-15, 42.5, 55)">
        <rect width="85" height="110" rx="8" fill="#1f1f1f" stroke="#333" strokeWidth="1" />
        <rect x="15" y="20" width="55" height="4" rx="2" fill="#2d2d2d" />
        <rect x="15" y="32" width="40" height="4" rx="2" fill="#2d2d2d" />
        <rect x="15" y="44" width="45" height="4" rx="2" fill="#2d2d2d" />
      </g>

      {/* Right Back Document (with folded corner) */}
      <g transform="translate(-30, -45) rotate(15, 42.5, 55)">
        <path d="M 0 8 A 8 8 0 0 1 8 0 H 60 L 85 25 V 102 A 8 8 0 0 1 77 110 H 8 A 8 8 0 0 1 0 102 Z" fill="#242424" stroke="#3a3a3a" strokeWidth="1" />
        <path d="M 60 0 V 17 A 8 8 0 0 0 68 25 H 85" fill="none" stroke="#3a3a3a" strokeWidth="1" />
        <polygon points="60,0 60,25 85,25" fill="#2d2d2d" />
        
        <rect x="15" y="20" width="30" height="4" rx="2" fill="#3d3d3d" />
        <rect x="15" y="32" width="55" height="4" rx="2" fill="#3d3d3d" />
        <rect x="15" y="44" width="40" height="4" rx="2" fill="#3d3d3d" />
      </g>

      {/* Front Document */}
      <g transform="translate(-42.5, -35)" filter="url(#docShadow)">
        <rect width="85" height="110" rx="8" fill="url(#docFront)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Header line */}
        <rect x="12" y="16" width="40" height="6" rx="3" fill="#6b7280" />
        <circle cx="67" cy="19" r="4" fill="#4b5563" />
        
        {/* Grid layout inside doc */}
        <rect x="12" y="32" width="28" height="20" rx="3" fill="#374151" />
        <rect x="44" y="32" width="28" height="20" rx="3" fill="#374151" />
        
        <rect x="12" y="56" width="60" height="10" rx="2" fill="#374151" />
        <rect x="12" y="70" width="60" height="10" rx="2" fill="#374151" />
        
        <rect x="12" y="86" width="30" height="6" rx="3" fill="#4b5563" />
      </g>

      {/* Excel-like Badge */}
      <g transform="translate(-60, 15)" filter="url(#badgeShadow)">
        <rect width="56" height="40" rx="8" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        {/* X Icon */}
        <g transform="translate(8, 8)">
          <rect width="16" height="24" rx="4" fill="#3f3f46" />
          <path d="M 4 6 L 12 18 M 12 6 L 4 18" stroke="#fff" strokeWidth="2.5" strokeLinecap="round" />
        </g>
        {/* Mini Grid */}
        <g transform="translate(30, 8)">
          <rect x="0" y="0" width="8" height="6" rx="1.5" fill="#9ca3af" />
          <rect x="10" y="0" width="10" height="6" rx="1.5" fill="#9ca3af" />
          
          <rect x="0" y="9" width="8" height="6" rx="1.5" fill="#6b7280" />
          <rect x="10" y="9" width="10" height="6" rx="1.5" fill="#6b7280" />
          
          <rect x="0" y="18" width="8" height="6" rx="1.5" fill="#4b5563" />
          <rect x="10" y="18" width="10" height="6" rx="1.5" fill="#4b5563" />
        </g>
      </g>
    </g>
  </svg>
);

const FolderWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="folderFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#404040" stopOpacity="1" />
        <stop offset="100%" stopColor="#222222" stopOpacity="1" />
      </linearGradient>
      <filter id="folderShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="15" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    
    <g transform="translate(45, 70)">
      {/* Folder Back */}
      <path d="M 0 12 A 6 6 0 0 1 6 6 H 45 C 50 6, 52 12, 55 16 L 58 20 H 144 A 6 6 0 0 1 150 26 V 100 A 6 6 0 0 1 144 106 H 6 A 6 6 0 0 1 0 100 Z" fill="#282828" stroke="#3a3a3a" strokeWidth="1" />
      
      {/* Paper sticking out */}
      <rect x="16" y="10" width="112" height="40" rx="4" fill="#a3a3a3" />
      
      {/* Folder Front */}
      <g filter="url(#folderShadow)">
        <path d="M 0 32 A 6 6 0 0 1 6 26 H 144 A 6 6 0 0 1 150 32 V 100 A 6 6 0 0 1 144 106 H 6 A 6 6 0 0 1 0 100 Z" fill="url(#folderFrontGrad)" stroke="rgba(255,255,255,0.12)" strokeWidth="1.5" />
        
        {/* Highlight inner top edge */}
        <path d="M 2 31 A 4 4 0 0 1 6 27 H 144 A 4 4 0 0 1 148 31" fill="none" stroke="rgba(255,255,255,0.2)" strokeWidth="1" />

        {/* Content inside Folder Front */}
        <g transform="translate(18, 58)">
          {/* Geometric K Logo */}
          <g transform="translate(0, 0)">
            <path d="M 10 2 L 2 9 L 10 16" fill="none" stroke="#f3f4f6" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" />
            <path d="M 18 2 L 10 9 L 18 16" fill="none" stroke="#9ca3af" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" />
          </g>
          {/* Text */}
          <text x="32" y="8" fill="#f3f4f6" fontSize="14" fontFamily="system-ui, sans-serif" fontWeight="600">Imported</text>
          <text x="32" y="24" fill="#9ca3af" fontSize="12" fontFamily="system-ui, sans-serif">3,721 records</text>
        </g>
      </g>
    </g>
  </svg>
);

const SyncWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="tableFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#2e2e2e" stopOpacity="1" />
        <stop offset="100%" stopColor="#1a1a1a" stopOpacity="1" />
      </linearGradient>
      <filter id="tableShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <filter id="syncBadgeShadow">
        <feDropShadow dx="0" dy="8" stdDeviation="8" floodColor="#000" floodOpacity="0.4" />
      </filter>
    </defs>

    <g transform="translate(30, 55)">
      {/* Back Table */}
      <g transform="translate(60, -10)">
        <rect width="110" height="75" rx="6" fill="#1f1f1f" stroke="#333" strokeWidth="1" />
        {/* Window controls */}
        <circle cx="12" cy="12" r="2.5" fill="#444" />
        <circle cx="20" cy="12" r="2.5" fill="#444" />
        <circle cx="28" cy="12" r="2.5" fill="#444" />
        <rect x="10" y="22" width="40" height="4" rx="2" fill="#3a3a3a" />
        
        {/* Table layout */}
        <rect x="10" y="32" width="90" height="33" rx="4" fill="#242424" stroke="#333" strokeWidth="1" />
        <line x1="10" y1="43" x2="100" y2="43" stroke="#333" strokeWidth="1" />
        <line x1="10" y1="54" x2="100" y2="54" stroke="#333" strokeWidth="1" />
        <line x1="40" y1="32" x2="40" y2="65" stroke="#333" strokeWidth="1" />
        <line x1="70" y1="32" x2="70" y2="65" stroke="#333" strokeWidth="1" />
      </g>

      {/* Front Table */}
      <g transform="translate(0, 30)" filter="url(#tableShadow)">
        <rect width="110" height="75" rx="6" fill="url(#tableFrontGrad)" stroke="rgba(255,255,255,0.12)" strokeWidth="1.5" />
        {/* Window controls */}
        <circle cx="12" cy="12" r="2.5" fill="#666" />
        <circle cx="20" cy="12" r="2.5" fill="#666" />
        <circle cx="28" cy="12" r="2.5" fill="#666" />
        <rect x="10" y="22" width="50" height="4" rx="2" fill="#555" />
        
        {/* Table layout */}
        <rect x="10" y="32" width="90" height="33" rx="4" fill="#292929" stroke="#444" strokeWidth="1" />
        <line x1="10" y1="43" x2="100" y2="43" stroke="#444" strokeWidth="1" />
        <line x1="10" y1="54" x2="100" y2="54" stroke="#444" strokeWidth="1" />
        <line x1="40" y1="32" x2="40" y2="65" stroke="#444" strokeWidth="1" />
        <line x1="70" y1="32" x2="70" y2="65" stroke="#444" strokeWidth="1" />
      </g>

      {/* Sync Badge */}
      <g transform="translate(56, 14)" filter="url(#syncBadgeShadow)">
        <rect width="56" height="56" rx="10" fill="rgba(30,30,30,0.95)" stroke="#9ca3af" strokeWidth="1.5" strokeDasharray="4 4" />
        
        {/* Swap Arrows */}
        <g transform="translate(15, 18)" stroke="#fff" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" fill="none">
          {/* Top Arrow (Points Left) */}
          <path d="M 2 4 H 24 M 2 4 L 8 0 M 2 4 L 8 8" />
          {/* Bottom Arrow (Points Right) */}
          <path d="M 24 16 H 2 M 24 16 L 18 12 M 24 16 L 18 20" />
        </g>
      </g>
    </g>
  </svg>
);

const ImageWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="imgFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="imgBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <filter id="imgShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
    </defs>

    <g transform="translate(120, 120)">
      {/* Back Image (Left) */}
      <g transform="translate(-45, -45) rotate(-12, 45, 40)">
        <rect width="90" height="85" rx="6" fill="#1f1f1f" stroke="#333" strokeWidth="1" />
        <rect x="6" y="6" width="78" height="55" rx="3" fill="#2d2d2d" />
        <circle cx="24" cy="24" r="8" fill="#3f3f46" />
        <path d="M 6 45 L 35 20 L 55 35 L 70 25 L 84 40 V 61 H 6 Z" fill="#3f3f46" />
        <rect x="10" y="68" width="40" height="4" rx="2" fill="#2d2d2d" />
      </g>

      {/* Front Image */}
      <g transform="translate(-30, -35)" filter="url(#imgShadow)">
        <rect width="90" height="85" rx="6" fill="url(#imgFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <rect x="6" y="6" width="78" height="55" rx="3" fill="#2a2a2a" stroke="#3a3a3a" strokeWidth="1" />
        
        {/* Sun */}
        <circle cx="60" cy="22" r="7" fill="#6b7280" />
        
        {/* Mountains */}
        <path d="M 6 50 L 30 25 L 50 45 L 65 30 L 84 52 V 61 H 6 Z" fill="#4b5563" />
        <path d="M 35 50 L 55 35 L 70 50 L 78 42 L 84 50 V 61 H 35 Z" fill="#374151" />
        
        {/* Details below image */}
        <rect x="10" y="68" width="45" height="5" rx="2.5" fill="#6b7280" />
        <rect x="10" y="76" width="25" height="3" rx="1.5" fill="#4b5563" />
      </g>

      {/* Image Badge (Camera) */}
      <g transform="translate(-55, 10)" filter="url(#imgBadgeShadow)">
        <rect width="48" height="40" rx="8" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        {/* Camera Icon */}
        <g transform="translate(12, 11)">
          <rect x="2" y="4" width="20" height="14" rx="3" fill="none" stroke="#fff" strokeWidth="2" />
          <circle cx="12" cy="11" r="4" fill="none" stroke="#fff" strokeWidth="2" />
          <path d="M 8 4 L 9 2 H 15 L 16 4" fill="none" stroke="#fff" strokeWidth="2" strokeLinejoin="round" />
        </g>
      </g>
    </g>
  </svg>
);

const ImageWidgetVariant2 = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="imgFrontGrad2" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="imgBadgeShadow2">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <filter id="imgShadow2">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
    </defs>

    <g transform="translate(120, 120)">
      {/* Front Image */}
      <g transform="translate(-45, -45)" filter="url(#imgShadow2)">
        <rect width="90" height="90" rx="6" fill="url(#imgFrontGrad2)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <rect x="6" y="6" width="78" height="78" rx="3" fill="#2a2a2a" stroke="#3a3a3a" strokeWidth="1" />
        
        {/* Sun */}
        <circle cx="60" cy="25" r="8" fill="#6b7280" />
        
        {/* Mountains */}
        <path d="M 6 60 L 30 35 L 50 55 L 65 40 L 84 62 V 84 H 6 Z" fill="#4b5563" />
        <path d="M 35 60 L 55 45 L 70 60 L 78 52 L 84 60 V 84 H 35 Z" fill="#374151" />
        
        {/* Crop Overlay Lines */}
        <path d="M 18 18 L 18 72 L 72 72 L 72 18 Z" fill="none" stroke="rgba(255,255,255,0.2)" strokeWidth="1" strokeDasharray="4 4" />
        
        {/* Crop Corners */}
        <path d="M 14 26 L 14 14 L 26 14" fill="none" stroke="#fff" strokeWidth="2.5" />
        <path d="M 14 64 L 14 76 L 26 76" fill="none" stroke="#fff" strokeWidth="2.5" />
        <path d="M 76 26 L 76 14 L 64 14" fill="none" stroke="#fff" strokeWidth="2.5" />
        <path d="M 76 64 L 76 76 L 64 76" fill="none" stroke="#fff" strokeWidth="2.5" />
      </g>

      {/* Image Badge (Crop Tool) */}
      <g transform="translate(-24, 25)" filter="url(#imgBadgeShadow2)">
        <rect width="48" height="48" rx="24" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        {/* Crop Icon */}
        <g transform="translate(13, 13)" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 6 2 L 6 14 A 2 2 0 0 0 8 16 L 20 16" />
          <path d="M 16 20 L 16 8 A 2 2 0 0 0 14 6 L 2 6" />
        </g>
      </g>
    </g>
  </svg>
);

const ImageWidgetVariant3 = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="imgFrontGrad3" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="imgBadgeShadow3">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <filter id="imgShadow3">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <clipPath id="splitClip3a">
        <rect x="6" y="6" width="39" height="55" />
      </clipPath>
      <clipPath id="splitClip3b">
        <rect x="45" y="6" width="39" height="55" />
      </clipPath>
    </defs>

    <g transform="translate(120, 120)">
      {/* Front Image */}
      <g transform="translate(-45, -45)" filter="url(#imgShadow3)">
        <rect width="90" height="85" rx="6" fill="url(#imgFrontGrad3)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <rect x="6" y="6" width="78" height="55" rx="3" fill="#2a2a2a" stroke="#3a3a3a" strokeWidth="1" />
        
        {/* Before side (Left) */}
        <g clipPath="url(#splitClip3a)">
           <circle cx="60" cy="22" r="7" fill="#4b5563" />
           <path d="M 6 50 L 30 25 L 50 45 L 65 30 L 84 52 V 61 H 6 Z" fill="#374151" />
        </g>

        {/* After side (Right) */}
        <g clipPath="url(#splitClip3b)">
           <circle cx="60" cy="22" r="7" fill="#9ca3af" />
           <path d="M 6 50 L 30 25 L 50 45 L 65 30 L 84 52 V 61 H 6 Z" fill="#6b7280" />
           <path d="M 35 50 L 55 35 L 70 50 L 78 42 L 84 50 V 61 H 35 Z" fill="#4b5563" />
        </g>
        
        <line x1="45" y1="6" x2="45" y2="61" stroke="#fff" strokeWidth="1.5" strokeDasharray="3 2" />

        {/* Details below image */}
        <rect x="10" y="68" width="45" height="5" rx="2.5" fill="#6b7280" />
        <rect x="10" y="76" width="25" height="3" rx="1.5" fill="#4b5563" />
      </g>

      {/* Image Badge (Sparkle/Stars) */}
      <g transform="translate(10, -25)" filter="url(#imgBadgeShadow3)">
        <rect width="44" height="44" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        {/* Sparkle Icon */}
        <g transform="translate(12, 12)" fill="#fff">
          <path d="M 10 0 L 11.5 6.5 L 18 8 L 11.5 9.5 L 10 16 L 8.5 9.5 L 2 8 L 8.5 6.5 Z" />
          <path d="M 3 1 L 3.5 3.5 L 6 4 L 3.5 4.5 L 3 7 L 2.5 4.5 L 0 4 L 2.5 3.5 Z" transform="translate(12, 9) scale(0.6)" />
        </g>
      </g>
    </g>
  </svg>
);

const ImageWidgetVariant4 = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="imgFrontGrad4" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="imgBadgeShadow4">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <filter id="imgShadow4">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
    </defs>

    <g transform="translate(120, 120)">
      {/* Back Image 1 (Left rotated) */}
      <g transform="translate(-35, -25) rotate(-20, 35, 35)">
        <rect width="70" height="70" rx="4" fill="#1f1f1f" stroke="#333" strokeWidth="1" />
        <rect x="4" y="4" width="62" height="62" rx="2" fill="#2d2d2d" />
      </g>
      
      {/* Back Image 2 (Right rotated) */}
      <g transform="translate(-35, -25) rotate(15, 35, 35)">
        <rect width="70" height="70" rx="4" fill="#242424" stroke="#3a3a3a" strokeWidth="1" />
        <rect x="4" y="4" width="62" height="62" rx="2" fill="#333" />
      </g>

      {/* Front Image */}
      <g transform="translate(-35, -25)" filter="url(#imgShadow4)">
        <rect width="70" height="70" rx="6" fill="url(#imgFrontGrad4)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <rect x="4" y="4" width="62" height="62" rx="3" fill="#2a2a2a" stroke="#3a3a3a" strokeWidth="1" />
        
        {/* Person/Portrait icon inside */}
        <circle cx="35" cy="28" r="12" fill="#4b5563" />
        <path d="M 15 62 C 15 48 25 44 35 44 C 45 44 55 48 55 62" fill="#4b5563" />
      </g>

      {/* Gallery Badge */}
      <g transform="translate(15, 15)" filter="url(#imgBadgeShadow4)">
        <rect width="40" height="40" rx="8" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        {/* Gallery Icon */}
        <g transform="translate(10, 10)" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="4" y="8" width="10" height="10" rx="2" />
          <path d="M 8 4 L 16 4 A 2 2 0 0 1 18 6 L 18 14" />
        </g>
      </g>
    </g>
  </svg>
);

const CameraWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="camFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <linearGradient id="lensGrad" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#222" />
        <stop offset="100%" stopColor="#0a0a0a" />
      </linearGradient>
      <filter id="camShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="camBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#camShadow)">
        {/* Top flash/viewfinder bump */}
        <path d="M -20 -25 L -12 -38 L 12 -38 L 20 -25 Z" fill="url(#camFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" strokeLinejoin="round" />
        {/* Main Body */}
        <rect x="-45" y="-25" width="90" height="60" rx="8" fill="url(#camFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        {/* Grip (Right side) */}
        <path d="M 30 -25 H 37 A 8 8 0 0 1 45 -17 V 27 A 8 8 0 0 1 37 35 H 30 Z" fill="#2d2d2d" stroke="rgba(255,255,255,0.1)" strokeWidth="1" />
        <line x1="34" y1="-15" x2="34" y2="25" stroke="#1a1a1a" strokeWidth="2" strokeLinecap="round" />
        <line x1="39" y1="-15" x2="39" y2="25" stroke="#1a1a1a" strokeWidth="2" strokeLinecap="round" />
        {/* Shutter button */}
        <rect x="-35" y="-30" width="14" height="6" rx="2" fill="#555" />
        <rect x="-32" y="-32" width="8" height="3" rx="1" fill="#ef4444" />
        
        {/* Lens Base */}
        <circle cx="0" cy="5" r="26" fill="#111" stroke="rgba(255,255,255,0.1)" strokeWidth="1.5" />
        {/* Lens Inner */}
        <circle cx="0" cy="5" r="20" fill="url(#lensGrad)" stroke="#444" strokeWidth="2" />
        <circle cx="0" cy="5" r="14" fill="#000" stroke="#333" strokeWidth="1" />
        {/* Lens reflection */}
        <circle cx="-5" cy="-2" r="5" fill="rgba(255,255,255,0.15)" />
        <circle cx="6" cy="12" r="2" fill="rgba(255,255,255,0.1)" />
      </g>

      {/* Aperture Badge */}
      <g transform="translate(25, -45)" filter="url(#camBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#fff" strokeWidth="1.5">
          <circle cx="10" cy="10" r="7" />
          <path d="M 10 3 L 13 8 M 17 10 L 12 13 M 15 16 L 9 14 M 10 17 L 7 12 M 3 10 L 8 7 M 5 4 L 11 6" strokeWidth="1" />
        </g>
      </g>
    </g>
  </svg>
);

const VideoCameraWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="vidFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="vidShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="vidBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#vidShadow)">
        {/* Lens hood */}
        <path d="M -40 -10 L -60 -20 L -60 30 L -40 20 Z" fill="#111" stroke="rgba(255,255,255,0.1)" strokeWidth="1.5" strokeLinejoin="round" />
        {/* Lens barrel */}
        <rect x="-45" y="-10" width="15" height="30" fill="#222" stroke="rgba(255,255,255,0.1)" strokeWidth="1.5" />
        {/* Main Body */}
        <rect x="-30" y="-15" width="80" height="45" rx="6" fill="url(#vidFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        {/* Top Handle */}
        <rect x="-10" y="-35" width="50" height="8" rx="4" fill="#2d2d2d" stroke="rgba(255,255,255,0.1)" strokeWidth="1" />
        <rect x="0" y="-27" width="8" height="12" fill="#2d2d2d" />
        <rect x="30" y="-27" width="8" height="12" fill="#2d2d2d" />
        {/* Side screen/panel */}
        <rect x="-10" y="-5" width="45" height="25" rx="3" fill="#1f1f1f" stroke="#444" strokeWidth="1.5" />
        <circle cx="5" cy="7" r="8" fill="#111" />
        <circle cx="20" cy="7" r="8" fill="#111" />
      </g>

      {/* REC Badge */}
      <g transform="translate(25, 20)" filter="url(#vidBadgeShadow)">
        <rect width="52" height="32" rx="16" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="14" cy="16" r="4" fill="#ef4444" />
        <text x="24" y="20" fill="#fff" fontSize="11" fontFamily="system-ui, sans-serif" fontWeight="700" letterSpacing="0.5">REC</text>
      </g>
    </g>
  </svg>
);

const InstantCameraWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="instFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="instShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="instBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 110)">
      {/* Photo sliding out (Back layer relative to bottom half) */}
      <g transform="translate(0, 45)" filter="url(#instShadow)">
        <rect x="-30" y="-10" width="60" height="70" rx="2" fill="#e5e7eb" />
        <rect x="-24" y="-4" width="48" height="48" fill="#1f2937" />
        <circle cx="10" cy="12" r="5" fill="#4b5563" />
        <path d="M -24 30 L -10 15 L 5 35 L 15 25 L 24 35 V 44 H -24 Z" fill="#374151" />
      </g>

      <g filter="url(#instShadow)">
        {/* Main Body */}
        <rect x="-40" y="-45" width="80" height="85" rx="8" fill="url(#instFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Rainbow stripe (Polaroid classic vibe) */}
        <rect x="-10" y="-45" width="4" height="85" fill="#ef4444" opacity="0.8" />
        <rect x="-6" y="-45" width="4" height="85" fill="#f59e0b" opacity="0.8" />
        <rect x="-2" y="-45" width="4" height="85" fill="#10b981" opacity="0.8" />
        <rect x="2" y="-45" width="4" height="85" fill="#3b82f6" opacity="0.8" />
        
        {/* Top viewfinder/flash area */}
        <rect x="15" y="-35" width="16" height="12" rx="2" fill="#111" stroke="#444" strokeWidth="1" />
        <circle cx="-20" cy="-29" r="6" fill="#111" stroke="#444" strokeWidth="1" />
        
        {/* Lens protrusion */}
        <circle cx="0" cy="5" r="22" fill="#2d2d2d" stroke="rgba(255,255,255,0.1)" strokeWidth="1.5" />
        <circle cx="0" cy="5" r="16" fill="#000" stroke="#444" strokeWidth="1.5" />
        <circle cx="0" cy="5" r="8" fill="#111" />
        <circle cx="-3" cy="2" r="3" fill="rgba(255,255,255,0.2)" />
        
        {/* Bottom Slot/Lip */}
        <rect x="-40" y="32" width="80" height="15" rx="4" fill="#2a2a2a" stroke="rgba(255,255,255,0.05)" strokeWidth="1" />
        <line x1="-32" y1="40" x2="32" y2="40" stroke="#000" strokeWidth="2" strokeLinecap="round" />
      </g>

      {/* Flash/Sparkle Badge */}
      <g transform="translate(-50, -60)" filter="url(#instBadgeShadow)">
        <rect width="40" height="40" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <path d="M 20 8 L 22 17 L 31 18 L 23 23 L 25 32 L 18 26 L 11 32 L 13 23 L 5 18 L 14 17 Z" fill="#fbbf24" stroke="#f59e0b" strokeWidth="1" strokeLinejoin="round" />
      </g>
    </g>
  </svg>
);

const CinematicCameraWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="cineFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="cineShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="cineBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#cineShadow)">
        {/* Back Reel */}
        <circle cx="-15" cy="-40" r="22" fill="#2d2d2d" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="-15" cy="-40" r="15" fill="none" stroke="#1f1f1f" strokeWidth="6" strokeDasharray="6 8" />
        <circle cx="-15" cy="-40" r="4" fill="#111" />
        
        {/* Front Reel */}
        <circle cx="20" cy="-35" r="24" fill="#333" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" />
        <circle cx="20" cy="-35" r="16" fill="none" stroke="#1f1f1f" strokeWidth="6" strokeDasharray="7 9" />
        <circle cx="20" cy="-35" r="4" fill="#111" />

        {/* Reel Mounts */}
        <path d="M -15 -20 L -10 -5 L -20 -5 Z" fill="#262626" />
        <path d="M 20 -15 L 25 -5 L 15 -5 Z" fill="#262626" />

        {/* Main Body */}
        <rect x="-40" y="-5" width="75" height="50" rx="6" fill="url(#cineFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Side Details */}
        <rect x="-30" y="5" width="40" height="30" rx="3" fill="#1f1f1f" stroke="#444" strokeWidth="1" />
        <line x1="-25" y1="12" x2="5" y2="12" stroke="#444" strokeWidth="2" strokeLinecap="round" />
        <line x1="-25" y1="18" x2="5" y2="18" stroke="#444" strokeWidth="2" strokeLinecap="round" />
        <line x1="-25" y1="24" x2="5" y2="24" stroke="#444" strokeWidth="2" strokeLinecap="round" />
        
        {/* Matte Box / Lens Base */}
        <path d="M 35 5 L 55 -5 L 55 45 L 35 35 Z" fill="#1f1f1f" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" strokeLinejoin="round" />
        <path d="M 55 -5 L 60 -10 L 60 50 L 55 45 Z" fill="#111" stroke="rgba(255,255,255,0.1)" strokeWidth="1" strokeLinejoin="round" />
        {/* Lens Inner Glass */}
        <ellipse cx="56" cy="20" rx="3" ry="15" fill="#000" />
        <ellipse cx="57" cy="15" rx="1" ry="5" fill="rgba(255,255,255,0.3)" />
        
        {/* Viewfinder/Eyepiece */}
        <path d="M -40 10 L -55 10 L -55 25 L -40 25 Z" fill="#222" stroke="#444" strokeWidth="1" />
        <circle cx="-55" cy="17.5" r="5" fill="#111" />
      </g>

      {/* Clapperboard Badge */}
      <g transform="translate(-45, 25)" filter="url(#cineBadgeShadow)">
        <rect width="40" height="35" rx="6" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(6, 6)">
          {/* Top striped bar */}
          <path d="M 0 0 L 28 0 L 28 6 L 0 6 Z" fill="#fff" />
          <path d="M 4 0 L 0 6 H 6 L 10 0 Z" fill="#111" />
          <path d="M 14 0 L 10 6 H 16 L 20 0 Z" fill="#111" />
          <path d="M 24 0 L 20 6 H 26 L 28 0 Z" fill="#111" />
          
          {/* Bottom board */}
          <rect x="0" y="8" width="28" height="15" rx="2" fill="#1f1f1f" stroke="#444" strokeWidth="1" />
          <line x1="4" y1="12" x2="16" y2="12" stroke="#444" strokeWidth="1" strokeLinecap="round" />
          <line x1="4" y1="16" x2="24" y2="16" stroke="#444" strokeWidth="1" strokeLinecap="round" />
        </g>
      </g>
    </g>
  </svg>
);

const CameraStackWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="camFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="camShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="camBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      {/* Left Back Photo */}
      <g transform="translate(-55, -45) rotate(-15, 42.5, 55)">
        <rect width="85" height="110" rx="4" fill="#1f1f1f" stroke="#333" strokeWidth="1" />
        <rect x="8" y="8" width="69" height="75" rx="2" fill="#2d2d2d" />
      </g>
      {/* Right Back Photo */}
      <g transform="translate(-30, -45) rotate(15, 42.5, 55)">
        <rect width="85" height="110" rx="4" fill="#242424" stroke="#3a3a3a" strokeWidth="1" />
        <rect x="8" y="8" width="69" height="75" rx="2" fill="#333" />
      </g>
      {/* Front Photo */}
      <g transform="translate(-42.5, -35)" filter="url(#camShadow)">
        <rect width="85" height="110" rx="4" fill="url(#camFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <rect x="8" y="8" width="69" height="75" rx="2" fill="#2a2a2a" stroke="#3a3a3a" strokeWidth="1" />
        <circle cx="42.5" cy="40" r="16" fill="#4b5563" />
        <path d="M 20 70 C 25 55, 60 55, 65 70" fill="none" stroke="#4b5563" strokeWidth="4" strokeLinecap="round" />
      </g>
      {/* Badge */}
      <g transform="translate(-60, 15)" filter="url(#camBadgeShadow)">
        <rect width="56" height="40" rx="8" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        {/* Camera Icon */}
        <g transform="translate(14, 11)" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="2" y="4" width="24" height="14" rx="3" />
          <circle cx="14" cy="11" r="4" />
          <path d="M 10 4 L 11 2 H 17 L 18 4" />
        </g>
      </g>
    </g>
  </svg>
);

const VideoPlayerWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="vidFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="vidShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="vidBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      {/* Back Window */}
      <g transform="translate(-20, -50)">
        <rect width="110" height="75" rx="6" fill="#1f1f1f" stroke="#333" strokeWidth="1" />
        <circle cx="12" cy="12" r="2.5" fill="#444" />
        <circle cx="20" cy="12" r="2.5" fill="#444" />
        <circle cx="28" cy="12" r="2.5" fill="#444" />
      </g>

      {/* Front Window */}
      <g transform="translate(-60, -20)" filter="url(#vidShadow)">
        <rect width="110" height="75" rx="6" fill="url(#vidFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        {/* macOS Dots */}
        <circle cx="12" cy="12" r="2.5" fill="#666" />
        <circle cx="20" cy="12" r="2.5" fill="#666" />
        <circle cx="28" cy="12" r="2.5" fill="#666" />
        {/* Player View */}
        <rect x="10" y="24" width="90" height="35" rx="2" fill="#292929" stroke="#444" strokeWidth="1" />
        <polygon points="50,34 60,41 50,48" fill="#6b7280" />
        {/* Timeline */}
        <rect x="10" y="65" width="90" height="4" rx="2" fill="#444" />
        <rect x="10" y="65" width="40" height="4" rx="2" fill="#9ca3af" />
        <circle cx="50" cy="67" r="4" fill="#fff" />
      </g>

      {/* Video Badge */}
      <g transform="translate(25, 20)" filter="url(#vidBadgeShadow)">
        <rect width="56" height="40" rx="8" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 12)" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="2" y="2" width="20" height="14" rx="3" />
          <polygon points="22,5 30,1 30,17 22,13" />
        </g>
      </g>
    </g>
  </svg>
);

const FilmStripWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="filmFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="filmShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="filmBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      {/* Back Strip */}
      <g transform="translate(-30, -70) rotate(-15)">
        <rect width="46" height="120" rx="4" fill="#1f1f1f" stroke="#333" strokeWidth="1" />
        {[0, 1, 2, 3, 4].map(i => (
          <g key={`back-${i}`}>
            <rect x="4" y={12 + i * 20} width="4" height="6" rx="1" fill="#111" />
            <rect x="38" y={12 + i * 20} width="4" height="6" rx="1" fill="#111" />
            <rect x="12" y={10 + i * 20} width="22" height="14" rx="1" fill="#2d2d2d" />
          </g>
        ))}
      </g>
      {/* Front Strip */}
      <g transform="translate(0, -50) rotate(10)" filter="url(#filmShadow)">
        <rect width="46" height="120" rx="4" fill="url(#filmFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        {[0, 1, 2, 3, 4].map(i => (
          <g key={`front-${i}`}>
            <rect x="4" y={12 + i * 20} width="4" height="6" rx="1" fill="#1a1a1a" />
            <rect x="38" y={12 + i * 20} width="4" height="6" rx="1" fill="#1a1a1a" />
            <rect x="12" y={10 + i * 20} width="22" height="14" rx="1" fill="#2a2a2a" />
          </g>
        ))}
      </g>
      {/* Badge (Clapperboard) */}
      <g transform="translate(-45, 10)" filter="url(#filmBadgeShadow)">
        <rect width="48" height="48" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 13)" fill="none" stroke="#fff" strokeWidth="2" strokeLinejoin="round">
          <path d="M 2 8 L 26 8 L 26 22 A 2 2 0 0 1 24 24 L 4 24 A 2 2 0 0 1 2 22 Z" />
          <path d="M 2 6 L 26 0 L 26 6 Z" strokeLinecap="round" />
        </g>
      </g>
    </g>
  </svg>
);

const MeetingGridWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="meetFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="meetShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="meetBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      {/* Main Window */}
      <g transform="translate(-55, -45)" filter="url(#meetShadow)">
        <rect width="110" height="90" rx="6" fill="url(#meetFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        {/* Window controls */}
        <circle cx="12" cy="12" r="2.5" fill="#666" />
        <circle cx="20" cy="12" r="2.5" fill="#666" />
        <circle cx="28" cy="12" r="2.5" fill="#666" />
        {/* 2x2 Grid */}
        <rect x="10" y="24" width="42" height="26" rx="2" fill="#292929" stroke="#3a3a3a" strokeWidth="1" />
        <circle cx="31" cy="37" r="6" fill="#4b5563" />
        <rect x="58" y="24" width="42" height="26" rx="2" fill="#292929" stroke="#3a3a3a" strokeWidth="1" />
        <circle cx="79" cy="37" r="6" fill="#4b5563" />
        <rect x="10" y="54" width="42" height="26" rx="2" fill="#292929" stroke="#3a3a3a" strokeWidth="1" />
        <circle cx="31" cy="67" r="6" fill="#4b5563" />
        <rect x="58" y="54" width="42" height="26" rx="2" fill="#292929" stroke="#3a3a3a" strokeWidth="1" />
        <circle cx="79" cy="67" r="6" fill="#4b5563" />
      </g>
      {/* Webcam Badge */}
      <g transform="translate(20, 25)" filter="url(#meetBadgeShadow)">
        <rect width="48" height="48" rx="24" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(12, 13)" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="12" cy="9" r="6" />
          <path d="M 12 15 V 20 M 6 20 H 18" />
        </g>
      </g>
    </g>
  </svg>
);

const LiveBroadcastWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="liveFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="liveShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="liveBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      {/* Main Window */}
      <g transform="translate(-65, -40)" filter="url(#liveShadow)">
        <rect width="130" height="80" rx="6" fill="url(#liveFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        {/* Main View */}
        <rect x="8" y="8" width="84" height="64" rx="4" fill="#2a2a2a" stroke="#3a3a3a" strokeWidth="1" />
        <circle cx="50" cy="40" r="14" fill="#4b5563" />
        {/* LIVE indicator */}
        <rect x="14" y="14" width="22" height="10" rx="2" fill="#ef4444" />
        <circle cx="20" cy="19" r="2" fill="#fff" />
        <path d="M 26 19 H 30 M 26 17 H 28" stroke="#fff" strokeWidth="1" strokeLinecap="round" />
        {/* Sidebar Chat */}
        <rect x="98" y="8" width="24" height="64" rx="4" fill="#242424" />
        <rect x="102" y="14" width="16" height="3" rx="1.5" fill="#4b5563" />
        <rect x="102" y="21" width="12" height="3" rx="1.5" fill="#4b5563" />
        <rect x="102" y="28" width="14" height="3" rx="1.5" fill="#4b5563" />
        <rect x="102" y="35" width="10" height="3" rx="1.5" fill="#4b5563" />
        <rect x="102" y="60" width="16" height="6" rx="2" fill="#374151" />
      </g>
      {/* Broadcast Badge */}
      <g transform="translate(-40, 20)" filter="url(#liveBadgeShadow)">
        <rect width="48" height="48" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(12, 12)" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round">
          <circle cx="12" cy="12" r="2.5" fill="#fff" />
          <path d="M 6 12 A 6 6 0 0 0 6 22 M 18 12 A 6 6 0 0 1 18 22" transform="translate(0, -5)" />
          <path d="M 2 12 A 10 10 0 0 0 2 28 M 22 12 A 10 10 0 0 1 22 28" transform="translate(0, -8)" />
        </g>
      </g>
    </g>
  </svg>
);

const DroneCameraWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="droneFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="droneShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="droneBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#droneShadow)">
        {/* Drone Arms & Props */}
        <g stroke="#1a1a1a" strokeWidth="4" strokeLinecap="round">
          {/* Top Left */}
          <line x1="-20" y1="-10" x2="-45" y2="-35" />
          <ellipse cx="-45" cy="-35" rx="15" ry="3" fill="#2d2d2d" stroke="none" />
          <line x1="-50" y1="-40" x2="-40" y2="-30" stroke="#444" strokeWidth="2" />
          
          {/* Top Right */}
          <line x1="20" y1="-10" x2="45" y2="-35" />
          <ellipse cx="45" cy="-35" rx="15" ry="3" fill="#2d2d2d" stroke="none" />
          <line x1="40" y1="-40" x2="50" y2="-30" stroke="#444" strokeWidth="2" />

          {/* Bottom Left */}
          <line x1="-20" y1="10" x2="-45" y2="35" />
          <ellipse cx="-45" cy="35" rx="15" ry="3" fill="#2d2d2d" stroke="none" />
          <line x1="-50" y1="30" x2="-40" y2="40" stroke="#444" strokeWidth="2" />

          {/* Bottom Right */}
          <line x1="20" y1="10" x2="45" y2="35" />
          <ellipse cx="45" cy="35" rx="15" ry="3" fill="#2d2d2d" stroke="none" />
          <line x1="40" y1="30" x2="50" y2="40" stroke="#444" strokeWidth="2" />
        </g>
        
        {/* Main Body */}
        <rect x="-30" y="-20" width="60" height="40" rx="8" fill="url(#droneFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Gimbal & Lens */}
        <rect x="-10" y="20" width="20" height="15" rx="3" fill="#1f1f1f" stroke="#444" strokeWidth="1" />
        <circle cx="0" cy="28" r="8" fill="#111" />
        <circle cx="0" cy="28" r="4" fill="#000" />
        <circle cx="-1.5" cy="26.5" r="1.5" fill="rgba(255,255,255,0.3)" />

        {/* LED Indicators */}
        <circle cx="-20" cy="-10" r="2" fill="#ef4444" />
        <circle cx="20" cy="-10" r="2" fill="#10b981" />
      </g>

      {/* Drone/Remote Badge */}
      <g transform="translate(15, 25)" filter="url(#droneBadgeShadow)">
        <rect width="40" height="35" rx="8" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 12)" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round">
          {/* Remote joysticks */}
          <rect x="0" y="0" width="20" height="12" rx="2" />
          <line x1="6" y1="6" x2="6" y2="3" />
          <circle cx="6" cy="3" r="1" fill="#fff" />
          <line x1="14" y1="6" x2="14" y2="3" />
          <circle cx="14" cy="3" r="1" fill="#fff" />
        </g>
      </g>
    </g>
  </svg>
);

const VintageProjectorWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="projFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="projShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="projBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <linearGradient id="beamGrad" x1="0" y1="0" x2="1" y2="0">
        <stop offset="0%" stopColor="rgba(255, 255, 255, 0.4)" />
        <stop offset="100%" stopColor="rgba(255, 255, 255, 0)" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#projShadow)">
        {/* Film Reels */}
        {/* Top Reel */}
        <circle cx="-15" cy="-45" r="20" fill="#2d2d2d" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="-15" cy="-45" r="14" fill="none" stroke="#1f1f1f" strokeWidth="4" strokeDasharray="5 7" />
        <circle cx="-15" cy="-45" r="3" fill="#111" />
        <path d="M -15 -25 L -10 -5 L -20 -5 Z" fill="#262626" />
        
        {/* Back Reel */}
        <circle cx="-45" cy="15" r="20" fill="#2d2d2d" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="-45" cy="15" r="14" fill="none" stroke="#1f1f1f" strokeWidth="4" strokeDasharray="5 7" />
        <circle cx="-45" cy="15" r="3" fill="#111" />
        <path d="M -25 15 L -5 10 L -5 20 Z" fill="#262626" />

        {/* Main Body */}
        <path d="M -25 -10 L 15 -10 L 20 40 L -25 40 Z" fill="url(#projFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" strokeLinejoin="round" />
        
        {/* Vents */}
        <line x1="-15" y1="0" x2="5" y2="0" stroke="#111" strokeWidth="2" strokeLinecap="round" />
        <line x1="-15" y1="8" x2="5" y2="8" stroke="#111" strokeWidth="2" strokeLinecap="round" />
        <line x1="-15" y1="16" x2="5" y2="16" stroke="#111" strokeWidth="2" strokeLinecap="round" />
        
        {/* Lens */}
        <path d="M 15 5 L 35 10 L 35 25 L 15 30 Z" fill="#1f1f1f" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" strokeLinejoin="round" />
        <rect x="35" y="8" width="10" height="19" rx="2" fill="#111" stroke="rgba(255,255,255,0.1)" strokeWidth="1" />
        
        {/* Light Beam */}
        <path d="M 46 17 L 90 -5 L 90 40 Z" fill="url(#beamGrad)" />
      </g>

      {/* Reel Badge */}
      <g transform="translate(-40, 20)" filter="url(#projBadgeShadow)">
        <rect width="40" height="40" rx="20" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round">
          <circle cx="10" cy="10" r="8" />
          <circle cx="10" cy="10" r="2" fill="#fff" />
          <path d="M 10 2 L 10 6 M 10 18 L 10 14 M 2 10 L 6 10 M 18 10 L 14 10" />
        </g>
      </g>
    </g>
  </svg>
);

const DirectorMonitorWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="monFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="monShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="monBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#monShadow)">
        {/* Antennas */}
        <line x1="-30" y1="-35" x2="-45" y2="-65" stroke="#444" strokeWidth="2" strokeLinecap="round" />
        <circle cx="-45" cy="-65" r="3" fill="#ef4444" />
        <line x1="30" y1="-35" x2="45" y2="-65" stroke="#444" strokeWidth="2" strokeLinecap="round" />
        <circle cx="45" cy="-65" r="3" fill="#ef4444" />

        {/* Main Body */}
        <rect x="-55" y="-35" width="110" height="75" rx="6" fill="url(#monFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Sun Hood Flaps */}
        <path d="M -55 -35 L -75 -15 L -75 55 L -55 35 Z" fill="#222" stroke="rgba(255,255,255,0.05)" strokeWidth="1" strokeLinejoin="round" />
        <path d="M 55 -35 L 75 -15 L 75 55 L 55 35 Z" fill="#222" stroke="rgba(255,255,255,0.05)" strokeWidth="1" strokeLinejoin="round" />
        <path d="M -55 -35 L -75 -15 L 75 -15 L 55 -35 Z" fill="#2a2a2a" stroke="rgba(255,255,255,0.05)" strokeWidth="1" strokeLinejoin="round" />

        {/* Screen */}
        <rect x="-45" y="-20" width="90" height="50" rx="2" fill="#111" stroke="#444" strokeWidth="1" />
        
        {/* Screen Content - Rule of Thirds Grid */}
        <path d="M -15 -20 L -15 30 M 15 -20 L 15 30 M -45 -3 L 45 -3 M -45 13 L 45 13" stroke="rgba(255,255,255,0.15)" strokeWidth="1" strokeDasharray="2 2" />
        {/* Focus Peaking / Scene Silhouette */}
        <circle cx="5" cy="5" r="10" fill="none" stroke="#ef4444" strokeWidth="1" />
        <path d="M -25 30 C -25 10, -5 10, 5 30" fill="#2d2d2d" />
        
        {/* Controls / Knobs on side */}
        <rect x="-45" y="32" width="12" height="4" rx="2" fill="#ef4444" />
        <circle cx="-15" cy="34" r="3" fill="#444" />
        <circle cx="-5" cy="34" r="3" fill="#444" />
        <circle cx="5" cy="34" r="3" fill="#444" />
      </g>

      {/* Frame Badge */}
      <g transform="translate(15, 20)" filter="url(#monBadgeShadow)">
        <rect width="44" height="44" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 4 8 L 4 4 L 8 4 M 16 4 L 20 4 L 20 8 M 20 16 L 20 20 L 16 20 M 8 20 L 4 20 L 4 16" />
          <circle cx="12" cy="12" r="2" fill="#fff" />
        </g>
      </g>
    </g>
  </svg>
);

const StudioLightingWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="lightFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="lightShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="lightBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#lightShadow)">
        {/* Tripod Stand */}
        <line x1="0" y1="20" x2="0" y2="65" stroke="#222" strokeWidth="4" strokeLinecap="round" />
        <line x1="0" y1="45" x2="-25" y2="75" stroke="#222" strokeWidth="4" strokeLinecap="round" />
        <line x1="0" y1="45" x2="25" y2="75" stroke="#222" strokeWidth="4" strokeLinecap="round" />
        
        {/* Pivot */}
        <circle cx="0" cy="18" r="4" fill="#444" />
        <rect x="-8" y="10" width="16" height="8" rx="2" fill="#222" />

        {/* Softbox back/side */}
        <path d="M -15 -25 L 15 -25 L 25 15 L -25 15 Z" fill="#111" stroke="#333" strokeWidth="1" />
        
        {/* Softbox Front Cover */}
        <path d="M -25 -35 L 25 -35 L 35 15 L -35 15 Z" fill="url(#lightFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" strokeLinejoin="round" />
        
        {/* Softbox Grid / Inner area */}
        <path d="M -20 -30 L 20 -30 L 30 10 L -30 10 Z" fill="#e5e7eb" opacity="0.9" />
        {/* Grid lines */}
        <line x1="-2" y1="-30" x2="-2" y2="10" stroke="#9ca3af" strokeWidth="1.5" />
        <line x1="6" y1="-30" x2="6" y2="10" stroke="#9ca3af" strokeWidth="1.5" />
        <line x1="-10" y1="-30" x2="-10" y2="10" stroke="#9ca3af" strokeWidth="1.5" />
        <line x1="14" y1="-30" x2="14" y2="10" stroke="#9ca3af" strokeWidth="1.5" />
        
        <line x1="-23" y1="-10" x2="23" y2="-10" stroke="#9ca3af" strokeWidth="1.5" />
        <line x1="-27" y1="0" x2="27" y2="0" stroke="#9ca3af" strokeWidth="1.5" />
      </g>

      {/* Light Badge */}
      <g transform="translate(10, 15)" filter="url(#lightBadgeShadow)">
        <rect width="40" height="40" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#fbbf24" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="10" cy="8" r="5" />
          <path d="M 7 13 L 7 15 C 7 16, 13 16, 13 15 L 13 13" />
          <line x1="10" y1="18" x2="10" y2="18" strokeWidth="3" />
        </g>
      </g>
    </g>
  </svg>
);

const GimbalStabilizerWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="gimbalFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="gimbalShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="gimbalBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#gimbalShadow)">
        {/* Handle */}
        <rect x="-8" y="25" width="16" height="45" rx="4" fill="#222" stroke="#444" strokeWidth="1" />
        <rect x="-8" y="30" width="16" height="2" fill="#111" />
        <rect x="-8" y="35" width="16" height="2" fill="#111" />
        <rect x="-8" y="40" width="16" height="2" fill="#111" />
        {/* Controls on handle */}
        <circle cx="0" cy="15" r="8" fill="url(#gimbalFrontGrad)" stroke="#444" strokeWidth="1" />
        <circle cx="0" cy="15" r="3" fill="#ef4444" />
        {/* Arms */}
        <path d="M 0 5 C 20 5, 30 -5, 30 -20" fill="none" stroke="#2a2a2a" strokeWidth="6" strokeLinecap="round" />
        <circle cx="30" cy="-20" r="5" fill="#444" />
        <path d="M 30 -20 C 30 -35, 15 -45, -5 -45" fill="none" stroke="#2a2a2a" strokeWidth="6" strokeLinecap="round" />
        <circle cx="-5" cy="-45" r="5" fill="#444" />
        {/* Camera on gimbal */}
        <rect x="-35" y="-35" width="45" height="30" rx="4" fill="url(#gimbalFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        {/* Lens */}
        <circle cx="-15" cy="-20" r="10" fill="#111" stroke="#333" strokeWidth="1.5" />
        <circle cx="-15" cy="-20" r="4" fill="#000" />
        <circle cx="-17" cy="-22" r="2" fill="rgba(255,255,255,0.2)" />
      </g>
      {/* Stabilizer Badge */}
      <g transform="translate(-40, 25)" filter="url(#gimbalBadgeShadow)">
        <rect width="40" height="40" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="10" cy="10" r="7" />
          <circle cx="10" cy="10" r="2" />
          <line x1="10" y1="0" x2="10" y2="3" />
          <line x1="10" y1="17" x2="10" y2="20" />
          <line x1="0" y1="10" x2="3" y2="10" />
          <line x1="17" y1="10" x2="20" y2="10" />
        </g>
      </g>
    </g>
  </svg>
);

const AudioBoomWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="audioFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="audioShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="audioBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#audioShadow)">
        {/* Boom pole */}
        <line x1="25" y1="40" x2="-15" y2="0" stroke="#222" strokeWidth="5" strokeLinecap="round" />
        
        {/* Shock mount */}
        <path d="M -20 -10 C -30 0, -40 -10, -30 -20" fill="none" stroke="#ef4444" strokeWidth="1.5" />
        <path d="M -10 -20 C -20 -30, -30 -20, -20 -10" fill="none" stroke="#ef4444" strokeWidth="1.5" />

        {/* Shotgun Mic (Rotated) */}
        <g transform="rotate(45, -20, -25)">
           <rect x="-45" y="-31" width="50" height="12" rx="6" fill="url(#audioFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
           <line x1="-35" y1="-29" x2="-35" y2="-21" stroke="#111" strokeWidth="1.5" />
           <line x1="-30" y1="-29" x2="-30" y2="-21" stroke="#111" strokeWidth="1.5" />
           <line x1="-25" y1="-29" x2="-25" y2="-21" stroke="#111" strokeWidth="1.5" />
           <line x1="-20" y1="-29" x2="-20" y2="-21" stroke="#111" strokeWidth="1.5" />
           <line x1="-15" y1="-29" x2="-15" y2="-21" stroke="#111" strokeWidth="1.5" />
           <line x1="-10" y1="-29" x2="-10" y2="-21" stroke="#111" strokeWidth="1.5" />
           {/* Cable */}
           <path d="M 5 -25 C 15 -25, 20 -10, 10 0 C 0 10, 20 20, 20 40" fill="none" stroke="#1a1a1a" strokeWidth="2" />
        </g>
      </g>
      
      {/* Headphones Badge */}
      <g transform="translate(15, -15)" filter="url(#audioBadgeShadow)">
        <rect width="40" height="40" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(8, 8)" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 4 14 v -4 a 8 8 0 0 1 16 0 v 4" />
          <rect x="2" y="10" width="4" height="8" rx="1" fill="#fff" />
          <rect x="18" y="10" width="4" height="8" rx="1" fill="#fff" />
        </g>
      </g>
    </g>
  </svg>
);

const ClapboardWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="clapFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="clapShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="clapBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#clapShadow)">
        {/* Main Board */}
        <rect x="-45" y="-10" width="90" height="60" rx="4" fill="url(#clapFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* White lines / Chalk marks */}
        <line x1="-35" y1="5" x2="-35" y2="40" stroke="rgba(255,255,255,0.2)" strokeWidth="1" />
        <line x1="15" y1="5" x2="15" y2="40" stroke="rgba(255,255,255,0.2)" strokeWidth="1" />
        <line x1="-35" y1="20" x2="35" y2="20" stroke="rgba(255,255,255,0.2)" strokeWidth="1" />
        
        {/* Text placeholders */}
        <rect x="-25" y="8" width="15" height="4" rx="2" fill="#6b7280" />
        <rect x="22" y="8" width="10" height="4" rx="2" fill="#6b7280" />
        <path d="M -25 32 Q -20 25, -15 32 T -5 32" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round" />
        <path d="M 22 28 C 28 28, 30 35, 26 35" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round" />

        {/* Top Strip (Base) */}
        <rect x="-45" y="-22" width="90" height="12" rx="2" fill="#111" stroke="#333" strokeWidth="1" />
        {/* Chevrons */}
        <path d="M -35 -22 L -25 -10 H -15 L -25 -22 Z" fill="#f3f4f6" />
        <path d="M -5 -22 L 5 -10 H 15 L 5 -22 Z" fill="#f3f4f6" />
        <path d="M 25 -22 L 35 -10 H 45 L 35 -22 Z" fill="#f3f4f6" />

        {/* Top Strip (Hinged part) */}
        <g transform="translate(-45, -22) rotate(-20)">
          <rect x="0" y="-12" width="90" height="12" rx="2" fill="#111" stroke="#333" strokeWidth="1" />
          <path d="M 10 -12 L 20 0 H 30 L 20 -12 Z" fill="#f3f4f6" />
          <path d="M 40 -12 L 50 0 H 60 L 50 -12 Z" fill="#f3f4f6" />
          <path d="M 70 -12 L 80 0 H 90 L 80 -12 Z" fill="#f3f4f6" />
          <circle cx="5" cy="-6" r="3" fill="#333" />
        </g>
      </g>
      
      {/* Star/Play Badge */}
      <g transform="translate(15, 20)" filter="url(#clapBadgeShadow)">
        <rect width="44" height="44" rx="22" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <polygon points="18,14 28,22 18,30" fill="#fff" />
      </g>
    </g>
  </svg>
);

const ActionCameraWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="actionFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="actionShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="actionBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#actionShadow)">
        {/* Cage/Mount */}
        <rect x="-40" y="-30" width="80" height="60" rx="6" fill="none" stroke="#222" strokeWidth="6" />
        <path d="M -15 30 L -15 45 M 15 30 L 15 45 M -15 45 H 15" stroke="#222" strokeWidth="6" strokeLinecap="round" strokeLinejoin="round" fill="none" />
        <circle cx="20" cy="40" r="5" fill="#444" />
        
        {/* Main Body */}
        <rect x="-35" y="-25" width="70" height="50" rx="8" fill="url(#actionFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Front Screen */}
        <rect x="-28" y="-15" width="22" height="22" rx="2" fill="#111" stroke="#3a3a3a" strokeWidth="1" />
        <text x="-17" y="-2" fill="#6b7280" fontSize="8" fontFamily="sans-serif" fontWeight="bold" textAnchor="middle">1080</text>
        <circle cx="-25" cy="-20" r="2" fill="#ef4444" />
        
        {/* Big Lens */}
        <circle cx="15" cy="-2" r="18" fill="#1f1f1f" stroke="#3a3a3a" strokeWidth="1.5" />
        <circle cx="15" cy="-2" r="12" fill="#000" />
        <circle cx="12" cy="-5" r="3" fill="rgba(255,255,255,0.3)" />
        <circle cx="17" cy="2" r="1.5" fill="rgba(255,255,255,0.15)" />
      </g>
      
      {/* Activity/Bolt Badge */}
      <g transform="translate(-40, 15)" filter="url(#actionBadgeShadow)">
        <rect width="36" height="36" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <path d="M 18 6 L 10 18 H 17 L 16 28 L 26 14 H 19 Z" fill="#eab308" />
      </g>
    </g>
  </svg>
);

const OldGamepadClassicWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="ogcFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#d1d5db" stopOpacity="1" />
        <stop offset="100%" stopColor="#9ca3af" stopOpacity="1" />
      </linearGradient>
      <filter id="ogcShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="ogcBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#ogcShadow)">
        {/* Main Body */}
        <path d="M -45 -65 L 45 -65 C 50 -65, 55 -60, 55 -55 L 55 50 C 55 60, 45 70, 35 70 L -35 70 C -45 70, -55 60, -55 50 L -55 -55 C -55 -60, -50 -65, -45 -65 Z" 
              fill="url(#ogcFrontGrad)" stroke="rgba(255,255,255,0.5)" strokeWidth="1.5" />
        
        {/* Screen Bezel */}
        <rect x="-40" y="-50" width="80" height="60" rx="4" fill="#4b5563" />
        {/* Screen */}
        <rect x="-30" y="-40" width="60" height="40" rx="2" fill="#84cc16" />
        <rect x="-28" y="-38" width="56" height="36" fill="#a3e635" />
        
        {/* D-Pad */}
        <g transform="translate(-25, 30)">
          <rect x="-12" y="-4" width="24" height="8" rx="1" fill="#1f2937" />
          <rect x="-4" y="-12" width="8" height="24" rx="1" fill="#1f2937" />
          <circle cx="0" cy="0" r="2" fill="#111827" />
        </g>
        
        {/* Action Buttons */}
        <g transform="translate(25, 25)">
          <circle cx="-12" cy="10" r="7" fill="#be185d" />
          <circle cx="10" cy="-2" r="7" fill="#be185d" />
        </g>
        
        {/* Select / Start */}
        <g transform="translate(0, 55)">
          <rect x="-15" y="0" width="10" height="4" rx="2" fill="#4b5563" transform="rotate(-20, -10, 2)" />
          <rect x="5" y="0" width="10" height="4" rx="2" fill="#4b5563" transform="rotate(-20, 10, 2)" />
        </g>
        
        {/* Speaker Grille */}
        <g transform="translate(30, 60) rotate(-45)">
          <line x1="-5" y1="-8" x2="5" y2="-8" stroke="#4b5563" strokeWidth="2" strokeLinecap="round" />
          <line x1="-5" y1="-4" x2="5" y2="-4" stroke="#4b5563" strokeWidth="2" strokeLinecap="round" />
          <line x1="-5" y1="0" x2="5" y2="0" stroke="#4b5563" strokeWidth="2" strokeLinecap="round" />
          <line x1="-5" y1="4" x2="5" y2="4" stroke="#4b5563" strokeWidth="2" strokeLinecap="round" />
        </g>
      </g>
      
      {/* Badge */}
      <g transform="translate(-15, -80)" filter="url(#ogcBadgeShadow)">
        <rect width="30" height="30" rx="8" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="15" cy="15" r="8" fill="none" stroke="#be185d" strokeWidth="2.5" />
      </g>
    </g>
  </svg>
);

const SunWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="brass" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#fef08a" />
        <stop offset="30%" stopColor="#d97706" />
        <stop offset="70%" stopColor="#f59e0b" />
        <stop offset="100%" stopColor="#78350f" />
      </linearGradient>
      <radialGradient id="sunCore" cx="35%" cy="35%" r="65%">
        <stop offset="0%" stopColor="#ffffff" />
        <stop offset="20%" stopColor="#fde047" />
        <stop offset="60%" stopColor="#ea580c" />
        <stop offset="100%" stopColor="#7c2d12" />
      </radialGradient>
      <filter id="sunGlow">
        <feDropShadow dx="0" dy="0" stdDeviation="12" floodColor="#f59e0b" floodOpacity="0.8" />
      </filter>
      <filter id="armillaryShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="12" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 110)">
      <g filter="url(#armillaryShadow)">
        {/* Base Stand */}
        <path d="M -35 100 L 35 100 C 40 100, 45 105, 45 115 L -45 115 C -45 105, -40 100, -35 100 Z" fill="url(#brass)" />
        <rect x="-12" y="70" width="24" height="30" fill="url(#brass)" />
        <ellipse cx="0" cy="70" rx="30" ry="8" fill="url(#brass)" />
        
        {/* Outer Ring */}
        <circle cx="0" cy="0" r="75" fill="none" stroke="url(#brass)" strokeWidth="8" />
        <circle cx="0" cy="0" r="75" fill="none" stroke="#78350f" strokeWidth="2" opacity="0.5" />
        
        {/* Axis Rod */}
        <line x1="0" y1="-80" x2="0" y2="80" stroke="url(#brass)" strokeWidth="6" strokeLinecap="round" />
      </g>

      {/* Back Arcs */}
      <path d="M -70 0 A 70 20 0 0 1 70 0" fill="none" stroke="url(#brass)" strokeWidth="6" opacity="0.4" />
      <g transform="rotate(35)">
        <path d="M -65 0 A 65 25 0 0 1 65 0" fill="none" stroke="url(#brass)" strokeWidth="5" opacity="0.4" />
      </g>
      <g transform="rotate(-45)">
        <path d="M -65 0 A 65 25 0 0 1 65 0" fill="none" stroke="url(#brass)" strokeWidth="5" opacity="0.4" />
      </g>

      {/* Sun Core */}
      <circle cx="0" cy="0" r="30" fill="url(#sunCore)" filter="url(#sunGlow)" />
      <circle cx="0" cy="0" r="30" fill="none" stroke="#fde047" strokeWidth="1.5" opacity="0.8" />
      
      {/* Solar Flares */}
      {[0, 60, 120, 180, 240, 300].map(angle => (
        <path key={angle} d="M 0 -30 Q 10 -45 0 -55 Q -8 -40 0 -30" fill="#f59e0b" opacity="0.7" filter="blur(2px)" transform={`rotate(${angle})`} />
      ))}

      {/* Front Arcs */}
      <path d="M -70 0 A 70 20 0 0 0 70 0" fill="none" stroke="url(#brass)" strokeWidth="6" />
      <path d="M -70 0 A 70 20 0 0 0 70 0" fill="none" stroke="#78350f" strokeWidth="1.5" opacity="0.5" />
      <g transform="rotate(35)">
        <path d="M -65 0 A 65 25 0 0 0 65 0" fill="none" stroke="url(#brass)" strokeWidth="5" />
      </g>
      <g transform="rotate(-45)">
        <path d="M -65 0 A 65 25 0 0 0 65 0" fill="none" stroke="url(#brass)" strokeWidth="5" />
      </g>
    </g>
  </svg>
);

const SkyWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="scopeBody" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#f3f4f6" />
        <stop offset="50%" stopColor="#9ca3af" />
        <stop offset="100%" stopColor="#374151" />
      </linearGradient>
      <linearGradient id="scopeBrass" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#fef08a" />
        <stop offset="50%" stopColor="#b45309" />
        <stop offset="100%" stopColor="#451a03" />
      </linearGradient>
      <linearGradient id="lensGlass" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#38bdf8" />
        <stop offset="50%" stopColor="#0284c7" />
        <stop offset="100%" stopColor="#082f49" />
      </linearGradient>
      <filter id="teleShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="15" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 130)">
      <g filter="url(#teleShadow)">
        {/* Tripod Back Leg */}
        <line x1="0" y1="20" x2="-15" y2="100" stroke="#4b5563" strokeWidth="8" strokeLinecap="round" />
        
        {/* Tripod Front Legs */}
        <line x1="0" y1="20" x2="-45" y2="110" stroke="#d1d5db" strokeWidth="10" strokeLinecap="round" />
        <line x1="0" y1="20" x2="45" y2="110" stroke="#d1d5db" strokeWidth="10" strokeLinecap="round" />
        <line x1="0" y1="20" x2="-45" y2="110" stroke="#9ca3af" strokeWidth="4" strokeLinecap="round" opacity="0.5" />
        <line x1="0" y1="20" x2="45" y2="110" stroke="#9ca3af" strokeWidth="4" strokeLinecap="round" opacity="0.5" />
        
        {/* Mount Mechanism */}
        <rect x="-12" y="-5" width="24" height="35" rx="4" fill="#1f2937" />
        <circle cx="0" cy="15" r="8" fill="url(#scopeBrass)" />
        <circle cx="0" cy="15" r="4" fill="#111827" />
        
        {/* Telescope Tube Group */}
        <g transform="rotate(-35) translate(0, -15)">
          {/* Main Body */}
          <rect x="-65" y="-22" width="130" height="44" rx="4" fill="url(#scopeBody)" />
          
          {/* Highlight/Shadow on body */}
          <rect x="-65" y="-20" width="130" height="8" rx="2" fill="#ffffff" opacity="0.4" />
          <rect x="-65" y="14" width="130" height="6" rx="2" fill="#111827" opacity="0.3" />
          
          {/* Lens Hood (Right) */}
          <path d="M 65 -25 L 85 -32 L 85 32 L 65 25 Z" fill="url(#scopeBody)" />
          <rect x="62" y="-23" width="8" height="46" rx="2" fill="url(#scopeBrass)" />
          
          {/* Lens Glass */}
          <ellipse cx="83" cy="0" rx="3" ry="28" fill="url(#lensGlass)" stroke="#0284c7" strokeWidth="1" />
          <path d="M 82 -20 C 85 -10, 85 10, 82 20" fill="none" stroke="#fff" strokeWidth="1.5" opacity="0.6" />
          
          {/* Eyepiece Assembly (Left) */}
          <path d="M -65 -12 L -80 -6 L -80 6 L -65 12 Z" fill="#1f2937" />
          <rect x="-90" y="-10" width="10" height="20" rx="2" fill="url(#scopeBrass)" />
          <rect x="-68" y="-15" width="6" height="30" rx="2" fill="url(#scopeBrass)" />
          <circle cx="-90" cy="0" r="5" fill="#111827" />
          
          {/* Finder Scope (Top) */}
          <g transform="translate(-10, -40)">
            <line x1="-15" y1="18" x2="-10" y2="8" stroke="url(#scopeBrass)" strokeWidth="4" />
            <line x1="15" y1="18" x2="10" y2="8" stroke="url(#scopeBrass)" strokeWidth="4" />
            <rect x="-25" y="0" width="50" height="12" rx="3" fill="url(#scopeBody)" />
            <rect x="-25" y="-1" width="6" height="14" rx="1" fill="url(#scopeBrass)" />
            <rect x="20" y="-1" width="6" height="14" rx="1" fill="url(#scopeBrass)" />
            <ellipse cx="26" cy="6" rx="1.5" ry="5" fill="url(#lensGlass)" />
          </g>
        </g>
      </g>
    </g>
  </svg>
);

const AtmosphereWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="globeWood" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#78350f" />
        <stop offset="50%" stopColor="#451a03" />
        <stop offset="100%" stopColor="#2c0a00" />
      </linearGradient>
      <linearGradient id="globeBrass" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#fef08a" />
        <stop offset="50%" stopColor="#b45309" />
        <stop offset="100%" stopColor="#451a03" />
      </linearGradient>
      <radialGradient id="globeWater" cx="30%" cy="30%" r="70%">
        <stop offset="0%" stopColor="#38bdf8" />
        <stop offset="50%" stopColor="#0284c7" />
        <stop offset="100%" stopColor="#082f49" />
      </radialGradient>
      <filter id="globeShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="15" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <clipPath id="globeClip">
        <circle cx="0" cy="0" r="62" />
      </clipPath>
    </defs>
    <g transform="translate(120, 105)">
      <g filter="url(#globeShadow)">
        {/* Base Stand */}
        <path d="M -40 110 L 40 110 C 45 110, 50 115, 50 125 L -50 125 C -50 115, -45 110, -40 110 Z" fill="url(#globeWood)" />
        <path d="M -25 90 L 25 90 L 30 110 L -30 110 Z" fill="url(#globeWood)" opacity="0.9" />
        <rect x="-10" y="55" width="20" height="35" fill="url(#globeBrass)" />
        <ellipse cx="0" cy="55" rx="30" ry="10" fill="url(#globeWood)" />
        <ellipse cx="0" cy="90" rx="35" ry="10" fill="url(#globeWood)" />
        
        {/* Meridian Bracket (Full circle) */}
        <circle cx="0" cy="0" r="72" fill="none" stroke="url(#globeBrass)" strokeWidth="6" />
        <circle cx="0" cy="0" r="72" fill="none" stroke="#78350f" strokeWidth="2" opacity="0.4" />
        
        {/* Bracket connection to stand */}
        <rect x="-6" y="70" width="12" height="15" fill="url(#globeBrass)" />

        {/* The Globe Sphere */}
        <g transform="rotate(-20)">
          {/* Water */}
          <circle cx="0" cy="0" r="62" fill="url(#globeWater)" />
          
          <g clipPath="url(#globeClip)">
            {/* Latitude / Longitude Lines */}
            <ellipse cx="0" cy="0" rx="62" ry="20" fill="none" stroke="#0ea5e9" strokeWidth="0.5" opacity="0.5" />
            <ellipse cx="0" cy="0" rx="20" ry="62" fill="none" stroke="#0ea5e9" strokeWidth="0.5" opacity="0.5" />
            <line x1="-62" y1="0" x2="62" y2="0" stroke="#0ea5e9" strokeWidth="0.5" opacity="0.5" />
            <line x1="0" y1="-62" x2="0" y2="62" stroke="#0ea5e9" strokeWidth="0.5" opacity="0.5" />

            {/* Continents */}
            <path d="M -30 -40 C -10 -40, -5 -20, -15 -10 C -25 0, -5 10, 5 35 C 15 60, -10 65, -20 45 C -30 25, -45 10, -50 -15 C -55 -40, -40 -40, -30 -40 Z" fill="#4ade80" />
            <path d="M -30 -40 C -10 -40, -5 -20, -15 -10 C -25 0, -5 10, 5 35 C 15 60, -10 65, -20 45 C -30 25, -45 10, -50 -15 C -55 -40, -40 -40, -30 -40 Z" fill="#22c55e" opacity="0.5" transform="translate(2, 2)" />
            
            <path d="M 20 -45 C 45 -45, 65 -15, 50 5 C 35 25, 55 40, 45 55 C 30 75, 10 40, 25 15 C 40 -10, 10 -20, 20 -45 Z" fill="#22c55e" />
            <path d="M 20 -45 C 45 -45, 65 -15, 50 5 C 35 25, 55 40, 45 55 C 30 75, 10 40, 25 15 C 40 -10, 10 -20, 20 -45 Z" fill="#16a34a" opacity="0.5" transform="translate(-2, 2)" />
            
            <path d="M -50 0 C -35 -15, -15 15, -40 25 C -55 35, -65 15, -50 0 Z" fill="#16a34a" />
            
            {/* Inner shadow for 3D sphere effect */}
            <circle cx="0" cy="0" r="62" fill="none" stroke="#000" strokeWidth="3" opacity="0.3" />
            <circle cx="0" cy="0" r="62" fill="#000" opacity="0.25" style={{ mixBlendMode: 'multiply' }} transform="translate(6, 6)" />
            {/* Highlight reflection */}
            <path d="M -40 -45 A 50 50 0 0 1 20 -55 A 60 60 0 0 0 -55 20 A 50 50 0 0 1 -40 -45 Z" fill="#fff" opacity="0.25" />
          </g>
        </g>
        
        {/* Axis Pins on Bracket */}
        <circle cx="-25" cy="-68" r="5" fill="url(#globeBrass)" />
        <circle cx="25" cy="68" r="5" fill="url(#globeBrass)" />
      </g>
    </g>
  </svg>
);

const FabricWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="fabBase" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#2a2a2a" />
        <stop offset="100%" stopColor="#1a1a1a" />
      </linearGradient>
      <pattern id="weavePattern" patternUnits="userSpaceOnUse" width="6" height="6" patternTransform="rotate(45)">
        <path d="M 0 3 L 6 3 M 3 0 L 3 6" stroke="rgba(255,255,255,0.04)" strokeWidth="1" />
      </pattern>
      <filter id="fabShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="20" floodColor="#000" floodOpacity="0.7" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#fabShadow)">
        {/* Background docs */}
        <g transform="rotate(-15) translate(0, 10)">
          <rect x="-45" y="-60" width="90" height="120" rx="8" fill="#141414" stroke="#2a2a2a" strokeWidth="1" />
        </g>
        <g transform="rotate(10) translate(10, 5)">
          <rect x="-45" y="-60" width="90" height="120" rx="8" fill="#1a1a1a" stroke="#333" strokeWidth="1" />
        </g>
        
        {/* Front doc */}
        <g transform="translate(-10, -5)">
          <rect x="-45" y="-60" width="90" height="120" rx="8" fill="url(#fabBase)" stroke="#444" strokeWidth="1.5" />
          <rect x="-45" y="-60" width="90" height="120" rx="8" fill="url(#weavePattern)" />
          
          {/* Folded corner */}
          <path d="M 25 -60 L 45 -60 L 45 -40 Z" fill="#1a1a1a" />
          <path d="M 25 -60 L 25 -40 L 45 -40 Z" fill="#333" stroke="#444" strokeWidth="1" strokeLinejoin="round" />
          
          {/* Faux content - Swatch grid */}
          <rect x="-35" y="-35" width="20" height="20" rx="4" fill="#333" />
          <rect x="-10" y="-35" width="20" height="20" rx="4" fill="#2a2a2a" />
          <rect x="15" y="-35" width="20" height="20" rx="4" fill="#444" />
          <rect x="-35" y="-10" width="20" height="20" rx="4" fill="#1a1a1a" />
          <rect x="-10" y="-10" width="20" height="20" rx="4" fill="#555" />
          <rect x="15" y="-10" width="20" height="20" rx="4" fill="#222" />
          <rect x="-35" y="15" width="70" height="15" rx="4" fill="#111" />
        </g>
      </g>
      
      {/* Badge */}
      <g transform="translate(-30, 5)" filter="url(#fabShadow)">
        <rect width="32" height="32" rx="8" fill="#222" stroke="#555" strokeWidth="1.5" />
        <text x="16" y="21" fill="#fff" fontSize="16" fontFamily="sans-serif" fontWeight="bold" textAnchor="middle">F</text>
      </g>
    </g>
  </svg>
);

const MetalWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="metalBase" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#2d2d2d" />
        <stop offset="100%" stopColor="#1a1a1a" />
      </linearGradient>
      <linearGradient id="metalShine" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="rgba(255,255,255,0.1)" />
        <stop offset="50%" stopColor="rgba(255,255,255,0)" />
        <stop offset="100%" stopColor="rgba(0,0,0,0.4)" />
      </linearGradient>
      <filter id="metalShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="20" floodColor="#000" floodOpacity="0.7" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#metalShadow)">
        {/* Back of folder */}
        <path d="M -60 -40 L -20 -40 L -10 -30 L 60 -30 L 60 40 C 60 45, 55 50, 50 50 L -50 50 C -55 50, -60 45, -60 40 Z" fill="#111" stroke="#333" strokeWidth="1" />
        
        {/* White paper sticking out */}
        <rect x="-50" y="-25" width="100" height="50" rx="4" fill="#d4d4d8" />
        
        {/* Front of folder */}
        <path d="M -60 -20 L 60 -20 L 60 40 C 60 45, 55 50, 50 50 L -50 50 C -55 50, -60 45, -60 40 Z" fill="url(#metalBase)" stroke="#555" strokeWidth="1.5" />
        <path d="M -60 -20 L 60 -20 L 60 40 C 60 45, 55 50, 50 50 L -50 50 C -55 50, -60 45, -60 40 Z" fill="url(#metalShine)" />
        
        {/* Metal specific details (rivets) */}
        <circle cx="-45" cy="-5" r="2" fill="#111" stroke="#555" strokeWidth="1" />
        <circle cx="45" cy="-5" r="2" fill="#111" stroke="#555" strokeWidth="1" />
        <circle cx="-45" cy="35" r="2" fill="#111" stroke="#555" strokeWidth="1" />
        <circle cx="45" cy="35" r="2" fill="#111" stroke="#555" strokeWidth="1" />
        
        {/* Text */}
        <text x="-5" y="17" fill="#fff" fontSize="12" fontFamily="sans-serif" fontWeight="500">Alloyed</text>
        <text x="-5" y="30" fill="#777" fontSize="10" fontFamily="sans-serif">Fe-C 4.2%</text>
      </g>
      
      {/* Badge (Hexagon) */}
      <g transform="translate(-45, 5)" filter="url(#metalShadow)">
        <path d="M 8 0 L 24 0 L 32 14 L 24 28 L 8 28 L 0 14 Z" fill="#222" stroke="#666" strokeWidth="1.5" />
        <text x="16" y="19" fill="#fff" fontSize="14" fontFamily="sans-serif" fontWeight="bold" textAnchor="middle">M</text>
      </g>
    </g>
  </svg>
);

const PlasticWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="plastBase" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#222" />
        <stop offset="100%" stopColor="#111" />
      </linearGradient>
      <filter id="plastShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="20" floodColor="#000" floodOpacity="0.7" />
      </filter>
      <filter id="plastBadgeShadow">
        <feDropShadow dx="0" dy="5" stdDeviation="5" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      {/* Back window */}
      <g transform="translate(15, -25)" filter="url(#plastShadow)">
        <rect x="-45" y="-35" width="90" height="70" rx="6" fill="#181818" stroke="#333" strokeWidth="1" />
        {/* Window header */}
        <circle cx="-35" cy="-25" r="2" fill="#555" />
        <circle cx="-28" cy="-25" r="2" fill="#555" />
        <circle cx="-21" cy="-25" r="2" fill="#555" />
        <rect x="-35" y="-15" width="70" height="40" rx="4" fill="#111" />
        {/* Grid lines */}
        <line x1="-11" y1="-15" x2="-11" y2="25" stroke="#222" strokeWidth="1" />
        <line x1="12" y1="-15" x2="12" y2="25" stroke="#222" strokeWidth="1" />
        <line x1="-35" y1="-2" x2="35" y2="-2" stroke="#222" strokeWidth="1" />
        <line x1="-35" y1="12" x2="35" y2="12" stroke="#222" strokeWidth="1" />
      </g>
      
      {/* Front window */}
      <g transform="translate(-30, 15)" filter="url(#plastShadow)">
        <rect x="-45" y="-35" width="90" height="70" rx="6" fill="url(#plastBase)" stroke="#444" strokeWidth="1" />
        {/* Window header */}
        <circle cx="-35" cy="-25" r="2" fill="#666" />
        <circle cx="-28" cy="-25" r="2" fill="#666" />
        <circle cx="-21" cy="-25" r="2" fill="#666" />
        <rect x="-35" y="-15" width="70" height="40" rx="4" fill="#0a0a0a" />
        {/* Grid lines */}
        <line x1="-11" y1="-15" x2="-11" y2="25" stroke="#1a1a1a" strokeWidth="1" />
        <line x1="12" y1="-15" x2="12" y2="25" stroke="#1a1a1a" strokeWidth="1" />
        <line x1="-35" y1="-2" x2="35" y2="-2" stroke="#1a1a1a" strokeWidth="1" />
        <line x1="-35" y1="12" x2="35" y2="12" stroke="#1a1a1a" strokeWidth="1" />
      </g>
      
      {/* Badge (Swap Arrows) */}
      <g transform="translate(5, -5)" filter="url(#plastBadgeShadow)">
        <rect width="36" height="36" rx="8" fill="#222" stroke="#666" strokeWidth="1" strokeDasharray="3 3" />
        {/* Swap Icon */}
        <path d="M 12 14 L 24 14 M 12 14 L 16 10 M 12 14 L 16 18" fill="none" stroke="#fff" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
        <path d="M 24 22 L 12 22 M 24 22 L 20 18 M 24 22 L 20 26" fill="none" stroke="#fff" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
      </g>
    </g>
  </svg>
);

const WoodWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="woodBase" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#24201c" />
        <stop offset="100%" stopColor="#141210" />
      </linearGradient>
      <filter id="woodShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="20" floodColor="#000" floodOpacity="0.7" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#woodShadow)">
        {/* Base card */}
        <rect x="-55" y="-55" width="110" height="110" rx="12" fill="url(#woodBase)" stroke="#3a3028" strokeWidth="1.5" />
        
        {/* Subtle wood grain paths */}
        <path d="M -40 -55 Q -30 -20 -45 10 T -35 55" fill="none" stroke="rgba(255,255,255,0.03)" strokeWidth="1" />
        <path d="M -20 -55 Q 0 -10 -15 20 T -5 55" fill="none" stroke="rgba(255,255,255,0.03)" strokeWidth="2" />
        <path d="M 0 -55 Q 30 0 10 30 T 25 55" fill="none" stroke="rgba(255,255,255,0.03)" strokeWidth="1" />
        
        {/* Content: Sliders / Bars */}
        <rect x="-35" y="-30" width="70" height="8" rx="4" fill="#111" />
        <rect x="-35" y="-30" width="45" height="8" rx="4" fill="#5a4d3f" />
        
        <rect x="-35" y="-10" width="70" height="8" rx="4" fill="#111" />
        <rect x="-35" y="-10" width="30" height="8" rx="4" fill="#5a4d3f" />
        
        <rect x="-35" y="10" width="70" height="8" rx="4" fill="#111" />
        <rect x="-35" y="10" width="55" height="8" rx="4" fill="#5a4d3f" />
      </g>
      
      {/* Badge */}
      <g transform="translate(10, 25)" filter="url(#woodShadow)">
        <rect width="32" height="32" rx="16" fill="#222" stroke="#5a4d3f" strokeWidth="1.5" />
        {/* Tree Icon */}
        <path d="M 16 8 L 24 18 L 8 18 Z" fill="none" stroke="#fff" strokeWidth="1.5" strokeLinejoin="round" />
        <line x1="16" y1="18" x2="16" y2="24" stroke="#fff" strokeWidth="1.5" strokeLinecap="round" />
      </g>
    </g>
  </svg>
);

const RingLightWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="ringFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="ringShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="ringBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#ringShadow)">
        <line x1="0" y1="20" x2="0" y2="70" stroke="#222" strokeWidth="4" strokeLinecap="round" />
        <line x1="0" y1="50" x2="-20" y2="75" stroke="#222" strokeWidth="4" strokeLinecap="round" />
        <line x1="0" y1="50" x2="20" y2="75" stroke="#222" strokeWidth="4" strokeLinecap="round" />
        
        <rect x="-6" y="20" width="12" height="8" rx="2" fill="#1f1f1f" stroke="#333" strokeWidth="1" />
        
        <circle cx="0" cy="-20" r="35" fill="url(#ringFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="0" cy="-20" r="20" fill="#111" stroke="#333" strokeWidth="1" />
        
        <circle cx="0" cy="-20" r="27.5" fill="none" stroke="#fdf4ff" strokeWidth="6" opacity="0.9" />
        
        <rect x="-8" y="-30" width="16" height="25" rx="2" fill="#2d2d2d" stroke="#444" strokeWidth="1" />
        <rect x="-6" y="-28" width="12" height="21" rx="1" fill="#111" />
      </g>
      
      <g transform="translate(15, -45)" filter="url(#ringBadgeShadow)">
        <rect width="36" height="36" rx="18" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(9, 9)" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="9" cy="9" r="4" />
          <path d="M 9 2 L 9 0 M 9 18 L 9 16 M 2 9 L 0 9 M 18 9 L 16 9 M 4 4 L 2.5 2.5 M 14 14 L 15.5 15.5 M 14 4 L 15.5 2.5 M 4 14 L 2.5 15.5" strokeWidth="1.5" />
        </g>
      </g>
    </g>
  </svg>
);

const SpotlightWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="spotFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="spotShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="spotBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <linearGradient id="spotBeam" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="rgba(255, 230, 150, 0.4)" />
        <stop offset="100%" stopColor="rgba(255, 230, 150, 0)" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#spotShadow)">
        <path d="M -35 -20 L -35 25 L 35 25 L 35 -20" fill="none" stroke="#222" strokeWidth="6" strokeLinejoin="round" />
        <rect x="-10" y="25" width="20" height="8" rx="2" fill="#1f1f1f" />
        
        <g transform="rotate(-30, 0, -20)">
          <path d="M -20 -40 L 20 -40 L 25 -10 L -25 -10 Z" fill="#1f1f1f" stroke="#333" strokeWidth="1" />
          <path d="M -25 -10 L 25 -10 L 30 20 L -30 20 Z" fill="url(#spotFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
          <ellipse cx="0" cy="20" rx="30" ry="8" fill="#111" stroke="#333" strokeWidth="1" />
          <ellipse cx="0" cy="20" rx="22" ry="5" fill="#fef08a" />
          <path d="M -22 20 L 22 20 L 60 100 L -60 100 Z" fill="url(#spotBeam)" />
        </g>
      </g>
      
      <g transform="translate(20, -45)" filter="url(#spotBadgeShadow)">
        <rect width="36" height="36" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(9, 9)" fill="none" stroke="#fcd34d" strokeWidth="2" strokeLinecap="round">
          <circle cx="9" cy="9" r="6" />
          <circle cx="9" cy="9" r="2" fill="#fcd34d" />
          <path d="M 0 9 H 2 M 16 9 H 18 M 9 0 V 2 M 9 16 V 18" />
        </g>
      </g>
    </g>
  </svg>
);

const LEDPanelWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="ledFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="ledShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="ledBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#ledShadow)">
        <line x1="0" y1="20" x2="0" y2="70" stroke="#222" strokeWidth="4" strokeLinecap="round" />
        
        <path d="M -45 5 L -45 25 L 45 25 L 45 5" fill="none" stroke="#222" strokeWidth="4" strokeLinejoin="round" />
        <rect x="-10" y="23" width="20" height="6" rx="2" fill="#1f1f1f" />
        
        <g transform="rotate(15, 0, -10)">
          <path d="M -30 -35 L 30 -35 L 40 -50 L -40 -50 Z" fill="#1f1f1f" stroke="#333" strokeWidth="1" />
          <path d="M -30 15 L 30 15 L 40 30 L -40 30 Z" fill="#1f1f1f" stroke="#333" strokeWidth="1" />
          <path d="M -35 -30 L -35 10 L -50 20 L -50 -40 Z" fill="#1f1f1f" stroke="#333" strokeWidth="1" />
          <path d="M 35 -30 L 35 10 L 50 20 L 50 -40 Z" fill="#1f1f1f" stroke="#333" strokeWidth="1" />

          <rect x="-35" y="-30" width="70" height="40" rx="4" fill="url(#ledFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
          <rect x="-30" y="-25" width="60" height="30" rx="2" fill="#111" />
          
          <g fill="#fef08a" opacity="0.9">
            {[...Array(5)].map((_, r) => 
              [...Array(11)].map((_, c) => 
                <circle key={`${r}-${c}`} cx={-25 + c * 5} cy={-21 + r * 5.5} r="1.5" />
              )
            )}
          </g>
        </g>
      </g>
      
      <g transform="translate(-45, -45)" filter="url(#ledBadgeShadow)">
        <rect width="36" height="36" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="#fff">
          <rect x="0" y="0" width="4" height="4" rx="1" />
          <rect x="6" y="0" width="4" height="4" rx="1" />
          <rect x="12" y="0" width="4" height="4" rx="1" />
          <rect x="0" y="6" width="4" height="4" rx="1" />
          <rect x="6" y="6" width="4" height="4" rx="1" />
          <rect x="12" y="6" width="4" height="4" rx="1" />
          <rect x="0" y="12" width="4" height="4" rx="1" />
          <rect x="6" y="12" width="4" height="4" rx="1" />
          <rect x="12" y="12" width="4" height="4" rx="1" />
        </g>
      </g>
    </g>
  </svg>
);

const FlashSpeedlightWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="flashFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="flashShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="flashBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#flashShadow)">
        <rect x="-10" y="55" width="20" height="8" rx="1" fill="#111" stroke="#333" strokeWidth="1" />
        <rect x="-12" y="61" width="24" height="4" fill="#222" />

        <rect x="-20" y="5" width="40" height="50" rx="4" fill="url(#flashFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        <rect x="-12" y="12" width="24" height="15" rx="2" fill="#111" />
        <circle cx="0" cy="38" r="6" fill="#2a2a2a" stroke="#444" strokeWidth="1" />
        <circle cx="0" cy="38" r="2" fill="#ef4444" />
        <rect x="-10" y="48" width="6" height="3" rx="1" fill="#333" />
        <rect x="4" y="48" width="6" height="3" rx="1" fill="#333" />

        <circle cx="0" cy="0" r="16" fill="#1f1f1f" stroke="#3a3a3a" strokeWidth="1" />

        <g transform="rotate(45, 0, 0)">
          <rect x="-18" y="-55" width="36" height="55" rx="4" fill="url(#flashFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
          
          <rect x="-14" y="-50" width="28" height="20" rx="2" fill="#111" stroke="#333" strokeWidth="1" />
          <rect x="-12" y="-48" width="24" height="16" rx="1" fill="#e5e7eb" opacity="0.9" />
          <line x1="-8" y1="-40" x2="8" y2="-40" stroke="#fff" strokeWidth="4" strokeLinecap="round" />
          
          <rect x="-8" y="-20" width="16" height="8" rx="2" fill="#ef4444" opacity="0.5" />
        </g>
      </g>
      
      <g transform="translate(15, 10)" filter="url(#flashBadgeShadow)">
        <rect width="36" height="36" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <path d="M 20 8 L 12 20 H 19 L 17 28 L 26 16 H 19 Z" fill="#fcd34d" />
      </g>
    </g>
  </svg>
);

const LightBulbWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="bulbFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="bulbShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="bulbBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <radialGradient id="bulbGlow" cx="50%" cy="50%" r="50%">
        <stop offset="0%" stopColor="#fef08a" stopOpacity="0.8" />
        <stop offset="100%" stopColor="#fef08a" stopOpacity="0.1" />
      </radialGradient>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#bulbShadow)">
        <path d="M -15 35 L 15 35 L 12 55 L -12 55 Z" fill="#71717a" stroke="#52525b" strokeWidth="1.5" strokeLinejoin="round" />
        <path d="M -12 55 L 12 55 L 5 65 L -5 65 Z" fill="#3f3f46" stroke="#27272a" strokeWidth="1" />
        <line x1="-14" y1="40" x2="14" y2="40" stroke="#3f3f46" strokeWidth="2" />
        <line x1="-13" y1="45" x2="13" y2="45" stroke="#3f3f46" strokeWidth="2" />
        <line x1="-12" y1="50" x2="12" y2="50" stroke="#3f3f46" strokeWidth="2" />

        <path d="M -18 35 C -18 20, -35 10, -35 -15 C -35 -40, -20 -55, 0 -55 C 20 -55, 35 -40, 35 -15 C 35 10, 18 20, 18 35 Z" 
              fill="url(#bulbFrontGrad)" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" />
        
        <circle cx="0" cy="-15" r="25" fill="url(#bulbGlow)" />
        
        <path d="M -8 35 L -8 0 M 8 35 L 8 0" fill="none" stroke="#666" strokeWidth="2" />
        <path d="M -8 0 L -15 -15 L 0 -25 L 15 -15 L 8 0" fill="none" stroke="#fcd34d" strokeWidth="1.5" strokeLinejoin="round" />
        
        <path d="M -25 -15 C -25 -30, -15 -45, 0 -45" fill="none" stroke="rgba(255,255,255,0.3)" strokeWidth="3" strokeLinecap="round" />
      </g>
      
      <g transform="translate(20, -20)" filter="url(#bulbBadgeShadow)">
        <rect width="36" height="36" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(18, 18)" fill="#fcd34d">
          <polygon points="-2,-8 2,-8 0,-12" />
          <polygon points="-2,8 2,8 0,12" />
          <polygon points="-8,-2 -8,2 -12,0" />
          <polygon points="8,-2 8,2 12,0" />
          <polygon points="-6,-6 -4,-4 -8,-8" stroke="#fcd34d" strokeWidth="2" strokeLinecap="round" />
          <polygon points="6,6 4,4 8,8" stroke="#fcd34d" strokeWidth="2" strokeLinecap="round" />
          <polygon points="-6,6 -4,4 -8,8" stroke="#fcd34d" strokeWidth="2" strokeLinecap="round" />
          <polygon points="6,-6 4,-4 8,-8" stroke="#fcd34d" strokeWidth="2" strokeLinecap="round" />
          <circle cx="0" cy="0" r="3" />
        </g>
      </g>
    </g>
  </svg>
);

const PointLightWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="pointFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <radialGradient id="pointGlow" cx="50%" cy="50%" r="50%">
        <stop offset="0%" stopColor="#fde047" stopOpacity="1" />
        <stop offset="20%" stopColor="#fde047" stopOpacity="0.8" />
        <stop offset="60%" stopColor="#d97706" stopOpacity="0.3" />
        <stop offset="100%" stopColor="#78350f" stopOpacity="0" />
      </radialGradient>
      <filter id="pointShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="pointBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#pointShadow)">
        {/* Base/Stand */}
        <path d="M -15 60 L 15 60 L 10 30 L -10 30 Z" fill="url(#pointFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <rect x="-4" y="10" width="8" height="20" fill="#2d2d2d" stroke="rgba(255,255,255,0.1)" strokeWidth="1" />
        
        {/* The Point Light Source */}
        <circle cx="0" cy="0" r="45" fill="url(#pointGlow)" />
        <circle cx="0" cy="0" r="10" fill="#ffffff" filter="drop-shadow(0 0 4px #fff)" />
        
        {/* Wireframe shell representing omnidirectional nature */}
        <circle cx="0" cy="0" r="20" fill="none" stroke="rgba(255,255,255,0.8)" strokeWidth="0.5" opacity="0.3" />
        <ellipse cx="0" cy="0" rx="20" ry="8" fill="none" stroke="rgba(255,255,255,0.8)" strokeWidth="0.5" opacity="0.3" />
        <ellipse cx="0" cy="0" rx="8" ry="20" fill="none" stroke="rgba(255,255,255,0.8)" strokeWidth="0.5" opacity="0.3" />
      </g>

      {/* Badge */}
      <g transform="translate(25, -45)" filter="url(#pointBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#fff" strokeWidth="1.5">
          <circle cx="10" cy="10" r="4" fill="#fbbf24" stroke="none" />
          <path d="M 10 2 L 10 4 M 10 16 L 10 18 M 2 10 L 4 10 M 16 10 L 18 10 M 4.3 4.3 L 5.7 5.7 M 15.7 15.7 L 14.3 14.3 M 15.7 4.3 L 14.3 5.7 M 4.3 15.7 L 5.7 14.3" stroke="#fbbf24" strokeWidth="1.5" strokeLinecap="round" />
        </g>
      </g>
    </g>
  </svg>
);

const CinematicLightWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="cineFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <radialGradient id="cineGlow" cx="50%" cy="50%" r="50%">
        <stop offset="0%" stopColor="#38bdf8" stopOpacity="0.9" />
        <stop offset="40%" stopColor="#0284c7" stopOpacity="0.5" />
        <stop offset="100%" stopColor="#082f49" stopOpacity="0" />
      </radialGradient>
      <filter id="cineShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="cineBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      {/* Light Beam Background */}
      <path d="M 0 -20 L 90 -80 A 100 100 0 0 1 90 40 Z" fill="url(#cineGlow)" opacity="0.6" />
      
      <g filter="url(#cineShadow)">
        {/* Yoke/Mount */}
        <path d="M -30 20 L -30 50 L 30 50 L 30 20" fill="none" stroke="#222" strokeWidth="8" strokeLinejoin="round" />
        <rect x="-8" y="50" width="16" height="30" fill="url(#cineFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Housing */}
        <rect x="-40" y="-30" width="60" height="60" rx="8" fill="url(#cineFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <rect x="-45" y="-20" width="10" height="40" rx="2" fill="#111" stroke="#333" strokeWidth="1" />
        
        {/* Lens */}
        <ellipse cx="20" cy="0" rx="6" ry="24" fill="#bae6fd" />
        <ellipse cx="20" cy="0" rx="6" ry="24" fill="url(#cineGlow)" />
        
        {/* Barn Doors */}
        {/* Top */}
        <path d="M 22 -24 L 50 -45 L 60 -35 L 22 -20 Z" fill="#2d2d2d" stroke="rgba(255,255,255,0.1)" strokeWidth="1.5" />
        {/* Bottom */}
        <path d="M 22 24 L 50 45 L 60 35 L 22 20 Z" fill="#2d2d2d" stroke="rgba(255,255,255,0.1)" strokeWidth="1.5" />
        {/* Side (facing viewer) */}
        <path d="M 22 -24 L 35 -15 L 35 15 L 22 24 Z" fill="#1a1a1a" stroke="rgba(255,255,255,0.1)" strokeWidth="1" />
        
        {/* Heat sinks */}
        <line x1="-30" y1="-25" x2="10" y2="-25" stroke="#111" strokeWidth="2" />
        <line x1="-30" y1="-15" x2="10" y2="-15" stroke="#111" strokeWidth="2" />
        <line x1="-30" y1="-5" x2="10" y2="-5" stroke="#111" strokeWidth="2" />
        <line x1="-30" y1="5" x2="10" y2="5" stroke="#111" strokeWidth="2" />
        <line x1="-30" y1="15" x2="10" y2="15" stroke="#111" strokeWidth="2" />
        <line x1="-30" y1="25" x2="10" y2="25" stroke="#111" strokeWidth="2" />
      </g>

      {/* Badge */}
      <g transform="translate(10, 30)" filter="url(#cineBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#38bdf8" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="2" y="4" width="16" height="12" rx="2" />
          <path d="M 18 6 L 22 4 L 22 16 L 18 14" fill="#38bdf8" />
        </g>
      </g>
    </g>
  </svg>
);

const AreaLightWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="areaFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <linearGradient id="areaPanel" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#ffffff" />
        <stop offset="100%" stopColor="#f1f5f9" />
      </linearGradient>
      <filter id="areaShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="areaBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <linearGradient id="areaGlow" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#ffffff" stopOpacity="0.8" />
        <stop offset="100%" stopColor="#ffffff" stopOpacity="0" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#areaShadow)">
        {/* Stand */}
        <line x1="0" y1="20" x2="0" y2="80" stroke="#222" strokeWidth="6" strokeLinecap="round" />
        <line x1="0" y1="60" x2="-20" y2="90" stroke="#222" strokeWidth="4" strokeLinecap="round" />
        <line x1="0" y1="60" x2="20" y2="90" stroke="#222" strokeWidth="4" strokeLinecap="round" />
        
        {/* Softbox body */}
        <path d="M -30 -40 L 30 -40 L 45 -20 L -45 -20 Z" fill="#111" stroke="#333" strokeWidth="1" />
        <path d="M -45 -20 L 45 -20 L 30 20 L -30 20 Z" fill="url(#areaFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Front Panel */}
        <rect x="-40" y="-15" width="80" height="30" rx="2" fill="url(#areaPanel)" />
        <rect x="-40" y="-15" width="80" height="30" rx="2" fill="url(#areaGlow)" filter="blur(2px)" />
        
        {/* Frame around panel */}
        <rect x="-40" y="-15" width="80" height="30" rx="2" fill="none" stroke="#2d2d2d" strokeWidth="2" />
        
        {/* Side flap/grid (honeycomb hint) */}
        <rect x="-42" y="-17" width="84" height="34" rx="3" fill="none" stroke="rgba(255,255,255,0.1)" strokeWidth="1.5" />
      </g>

      {/* Badge */}
      <g transform="translate(-40, 15)" filter="url(#areaBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="2" y="6" width="16" height="8" rx="1" />
          <line x1="10" y1="14" x2="10" y2="18" />
          <line x1="6" y1="18" x2="14" y2="18" />
        </g>
      </g>
    </g>
  </svg>
);

const SingleLEDWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="ledFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <linearGradient id="ledResin" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#ef4444" stopOpacity="0.95" />
        <stop offset="100%" stopColor="#991b1b" stopOpacity="0.95" />
      </linearGradient>
      <radialGradient id="ledCore" cx="40%" cy="30%" r="50%">
        <stop offset="0%" stopColor="#ffffff" />
        <stop offset="40%" stopColor="#fca5a5" />
        <stop offset="100%" stopColor="#dc2626" stopOpacity="0" />
      </radialGradient>
      <filter id="ledGlow">
        <feDropShadow dx="0" dy="0" stdDeviation="15" floodColor="#ef4444" floodOpacity="0.6" />
      </filter>
      <filter id="ledShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="ledBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 110)">
      <g filter="url(#ledShadow)">
        {/* Metal Legs (Anode & Cathode) */}
        <line x1="-10" y1="30" x2="-10" y2="100" stroke="#737373" strokeWidth="4" strokeLinecap="round" />
        <line x1="10" y1="30" x2="10" y2="85" stroke="#737373" strokeWidth="4" strokeLinecap="round" />
        
        {/* Base flange styled with camera theme */}
        <ellipse cx="0" cy="30" rx="24" ry="6" fill="url(#ledFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <rect x="-24" y="27" width="48" height="3" fill="#111" />
        
        {/* Epoxy Lens Body */}
        <path d="M -22 27 L -22 0 C -22 -20, -15 -35, 0 -35 C 15 -35, 22 -20, 22 0 L 22 27 Z" fill="url(#ledResin)" filter="url(#ledGlow)" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" />
        
        {/* Internal Anvil & Post */}
        <path d="M -10 30 L -10 10 L -4 10 L -4 5 L -10 5 L -10 0 L -4 0 L -10 -8 L -15 -8 L -10 30 Z" fill="#d4d4d4" opacity="0.9" />
        <path d="M 10 30 L 10 -2 L 5 -2 L 10 30 Z" fill="#d4d4d4" opacity="0.9" />
        
        {/* Core Light Emission */}
        <circle cx="-2" cy="-4" r="8" fill="url(#ledCore)" />
        
        {/* Reflections */}
        <path d="M -15 15 L -15 0 C -15 -12, -8 -25, 0 -25" fill="none" stroke="#fff" strokeWidth="2.5" strokeLinecap="round" opacity="0.5" />
      </g>

      {/* Badge */}
      <g transform="translate(20, -30)" filter="url(#ledBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#ef4444" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="10" cy="8" r="4" fill="#ef4444" opacity="0.2" />
          <line x1="8" y1="12" x2="8" y2="18" />
          <line x1="12" y1="12" x2="12" y2="16" />
        </g>
      </g>
    </g>
  </svg>
);

const IESLightWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="iesFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="iesShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="iesBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      {/* Intricate scalloped IES profile */}
      <linearGradient id="iesBeamCenter" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#fef08a" stopOpacity="0.9" />
        <stop offset="50%" stopColor="#fef08a" stopOpacity="0.4" />
        <stop offset="100%" stopColor="#fef08a" stopOpacity="0" />
      </linearGradient>
      <linearGradient id="iesBeamSide" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#fde047" stopOpacity="0.6" />
        <stop offset="100%" stopColor="#fde047" stopOpacity="0" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 60)">
      {/* The Wall surface it casts on */}
      <rect x="-100" y="-40" width="200" height="200" fill="#111" rx="8" />
      
      {/* Light Pattern (IES Profile) */}
      <g style={{ mixBlendMode: 'screen' }}>
        {/* Center strong lobe */}
        <path d="M -10 10 Q 0 100, 0 160 Q 0 100, 10 10 Z" fill="url(#iesBeamCenter)" filter="blur(4px)" />
        {/* Main wide scallop */}
        <path d="M -20 10 Q -60 50, -40 120 Q 0 60, 40 120 Q 60 50, 20 10 Z" fill="url(#iesBeamCenter)" filter="blur(8px)" opacity="0.7" />
        {/* Secondary side lobes */}
        <path d="M -25 10 C -80 30, -70 80, -90 90 C -60 50, -30 20, -25 10 Z" fill="url(#iesBeamSide)" filter="blur(6px)" />
        <path d="M 25 10 C 80 30, 70 80, 90 90 C 60 50, 30 20, 25 10 Z" fill="url(#iesBeamSide)" filter="blur(6px)" />
        {/* Top grazing light */}
        <ellipse cx="0" cy="10" rx="35" ry="8" fill="#fef08a" filter="blur(3px)" opacity="0.8" />
      </g>
      
      {/* The Fixture */}
      <g filter="url(#iesShadow)">
        <rect x="-25" y="-15" width="50" height="25" rx="4" fill="url(#iesFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <rect x="-20" y="-10" width="40" height="15" fill="#111" stroke="#333" strokeWidth="1" />
        <line x1="-15" y1="10" x2="15" y2="10" stroke="#fef08a" strokeWidth="2" filter="blur(1px)" />
      </g>

      {/* Badge */}
      <g transform="translate(25, -25)" filter="url(#iesBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#fef08a" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 4 8 L 10 2 L 16 8" />
          <path d="M 4 16 L 10 10 L 16 16" />
          <line x1="10" y1="2" x2="10" y2="10" />
        </g>
      </g>
    </g>
  </svg>
);
const DeskLampWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="deskFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="deskShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="deskBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <linearGradient id="deskBeam" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="rgba(255, 230, 150, 0.4)" />
        <stop offset="100%" stopColor="rgba(255, 230, 150, 0)" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#deskShadow)">
        <path d="M -30 65 L 30 65 L 25 75 L -25 75 Z" fill="#1f1f1f" stroke="#333" strokeWidth="1" />
        <ellipse cx="0" cy="65" rx="30" ry="10" fill="url(#deskFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="20" cy="65" r="3" fill="#ef4444" />
        
        <path d="M 0 60 L -15 20 M -15 20 L 15 -20" fill="none" stroke="#555" strokeWidth="6" strokeLinecap="round" strokeLinejoin="round" />
        <circle cx="-15" cy="20" r="6" fill="#111" stroke="#444" strokeWidth="1.5" />
        <circle cx="15" cy="-20" r="6" fill="#111" stroke="#444" strokeWidth="1.5" />
        <path d="M 0 60 L -15 20 M -15 20 L 15 -20" fill="none" stroke="#222" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" />
        
        <g transform="translate(15, -20) rotate(30)">
          <path d="M -15 0 C -15 -20, 15 -20, 15 0 L 25 35 L -25 35 Z" fill="url(#deskFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
          <ellipse cx="0" cy="35" rx="25" ry="6" fill="#111" stroke="#333" strokeWidth="1" />
          <ellipse cx="0" cy="35" rx="15" ry="3" fill="#fef08a" />
          <path d="M -15 35 L 15 35 L 50 110 L -50 110 Z" fill="url(#deskBeam)" />
        </g>
      </g>
      
      <g transform="translate(-40, -45)" filter="url(#deskBadgeShadow)">
        <rect width="36" height="36" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(9, 9)" fill="none" stroke="#fcd34d" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 0 18 L 18 18" />
          <path d="M 9 18 L 4 6 L 14 6 Z" />
          <circle cx="9" cy="6" r="3" fill="#fcd34d" />
        </g>
      </g>
    </g>
  </svg>
);

const LanternWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="lanternFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#8b3a3a" stopOpacity="1" />
        <stop offset="100%" stopColor="#4a1a1a" stopOpacity="1" />
      </linearGradient>
      <linearGradient id="lanternGlass" x1="0" y1="0" x2="1" y2="0">
        <stop offset="0%" stopColor="rgba(255,255,255,0.1)" />
        <stop offset="50%" stopColor="rgba(255,255,255,0.3)" />
        <stop offset="100%" stopColor="rgba(255,255,255,0.1)" />
      </linearGradient>
      <filter id="lanternShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="lanternBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <radialGradient id="lanternGlow" cx="50%" cy="50%" r="50%">
        <stop offset="0%" stopColor="#fca5a5" stopOpacity="0.9" />
        <stop offset="100%" stopColor="#fca5a5" stopOpacity="0" />
      </radialGradient>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#lanternShadow)">
        <path d="M -25 -65 C -45 -65, -45 -10, -45 -10 L 45 -10 C 45 -10, 45 -65, 25 -65" fill="none" stroke="#222" strokeWidth="4" strokeLinecap="round" />
        <circle cx="0" cy="-65" r="4" fill="#111" stroke="#333" strokeWidth="1" />

        <path d="M -25 45 L 25 45 L 35 65 L -35 65 Z" fill="url(#lanternFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <ellipse cx="0" cy="65" rx="35" ry="8" fill="#4a1a1a" stroke="#222" strokeWidth="1" />
        <ellipse cx="0" cy="45" rx="25" ry="6" fill="#222" />

        <path d="M -20 -35 L 20 -35 L 25 45 L -25 45 Z" fill="url(#lanternGlass)" stroke="rgba(255,255,255,0.2)" strokeWidth="1" />
        
        <circle cx="0" cy="15" r="25" fill="url(#lanternGlow)" />
        <path d="M 0 5 C 10 5, 10 25, 0 25 C -10 25, -10 5, 0 5 Z" fill="#fef08a" />
        <path d="M 0 10 C 5 10, 5 25, 0 25 C -5 25, -5 10, 0 10 Z" fill="#fff" />
        <rect x="-3" y="25" width="6" height="20" rx="2" fill="#222" />

        <path d="M -30 -25 L 30 -25 L 20 -35 L -20 -35 Z" fill="url(#lanternFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <ellipse cx="0" cy="-25" rx="30" ry="8" fill="#4a1a1a" stroke="#222" strokeWidth="1" />
        
        <path d="M -15 -35 C -15 -50, 15 -50, 15 -35 Z" fill="url(#lanternFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="0" cy="-52" r="6" fill="#222" stroke="#444" strokeWidth="1" />
        
        <path d="M -22 -25 L -27 45 M 22 -25 L 27 45" fill="none" stroke="#111" strokeWidth="2" />
      </g>
      
      <g transform="translate(15, 15)" filter="url(#lanternBadgeShadow)">
        <rect width="36" height="36" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(9, 9)" fill="none" stroke="#f87171" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 9 18 C 9 18, 1 12, 1 6 C 1 2, 9 0, 9 0 C 9 0, 17 2, 17 6 C 17 12, 9 18, 9 18 Z" fill="#f87171" opacity="0.3" />
          <path d="M 9 18 C 9 18, 5 14, 5 9 C 5 5, 9 3, 9 3 C 9 3, 13 5, 13 9 C 13 14, 9 18, 9 18 Z" fill="#f87171" />
        </g>
      </g>
    </g>
  </svg>
);

const CandleWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="candleFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#fdf4ff" stopOpacity="1" />
        <stop offset="100%" stopColor="#e5e7eb" stopOpacity="1" />
      </linearGradient>
      <linearGradient id="holderGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#b45309" stopOpacity="1" />
        <stop offset="100%" stopColor="#78350f" stopOpacity="1" />
      </linearGradient>
      <filter id="candleShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="candleBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <radialGradient id="candleGlow" cx="50%" cy="50%" r="50%">
        <stop offset="0%" stopColor="#fcd34d" stopOpacity="0.8" />
        <stop offset="100%" stopColor="#fcd34d" stopOpacity="0" />
      </radialGradient>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#candleShadow)">
        <path d="M 25 65 C 45 65, 45 45, 25 45" fill="none" stroke="url(#holderGrad)" strokeWidth="6" strokeLinecap="round" />
        <ellipse cx="0" cy="70" rx="35" ry="10" fill="url(#holderGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <path d="M -15 65 L 15 65 L 12 55 L -12 55 Z" fill="#78350f" />
        
        <rect x="-12" y="-10" width="24" height="65" fill="url(#candleFrontGrad)" stroke="#d1d5db" strokeWidth="1" />
        <ellipse cx="0" cy="-10" rx="12" ry="4" fill="#f3f4f6" stroke="#d1d5db" strokeWidth="1" />
        
        <path d="M -12 -10 C -12 0, -5 5, -8 15 C -5 10, -5 0, -12 -10 Z" fill="#fdf4ff" />
        <path d="M 12 -10 C 12 5, 8 10, 10 25 C 8 20, 6 5, 12 -10 Z" fill="#fdf4ff" />
        <path d="M 0 -10 C 5 0, 0 10, 2 20 C 0 10, -3 0, 0 -10 Z" fill="#fdf4ff" />

        <path d="M -1 -10 L 1 -10 L 0 -20 Z" fill="#444" />
        
        <circle cx="0" cy="-35" r="25" fill="url(#candleGlow)" />
        <path d="M 0 -50 C 10 -50, 10 -25, 0 -20 C -10 -25, -10 -50, 0 -50 Z" fill="#fcd34d" />
        <path d="M 0 -45 C 5 -45, 5 -25, 0 -22 C -5 -25, -5 -45, 0 -45 Z" fill="#fff" />
      </g>
      
      <g transform="translate(-40, -45)" filter="url(#candleBadgeShadow)">
        <rect width="36" height="36" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(9, 9)" fill="none" stroke="#fbbf24" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 9 18 C 9 18, 1 10, 1 5 C 1 1, 9 0, 9 0 C 9 0, 17 1, 17 5 C 17 10, 9 18, 9 18 Z" fill="#fbbf24" opacity="0.3" />
          <path d="M 9 18 C 9 18, 5 12, 5 8 C 5 4, 9 2, 9 2 C 9 2, 13 4, 13 8 C 13 12, 9 18, 9 18 Z" fill="#fbbf24" />
        </g>
      </g>
    </g>
  </svg>
);

const DiscoBallWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="discoGrad" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#e2e8f0" />
        <stop offset="50%" stopColor="#94a3b8" />
        <stop offset="100%" stopColor="#475569" />
      </linearGradient>
      <filter id="discoShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="discoBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <linearGradient id="discoBeam1" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="rgba(244, 114, 182, 0.6)" />
        <stop offset="100%" stopColor="rgba(244, 114, 182, 0)" />
      </linearGradient>
      <linearGradient id="discoBeam2" x1="0" y1="0" x2="-1" y2="1">
        <stop offset="0%" stopColor="rgba(56, 189, 248, 0.6)" />
        <stop offset="100%" stopColor="rgba(56, 189, 248, 0)" />
      </linearGradient>
      <linearGradient id="discoBeam3" x1="0" y1="0" x2="0" y2="-1">
        <stop offset="0%" stopColor="rgba(167, 243, 208, 0.6)" />
        <stop offset="100%" stopColor="rgba(167, 243, 208, 0)" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#discoShadow)">
        <path d="M 0 0 L 50 100 L 70 100 Z" fill="url(#discoBeam1)" />
        <path d="M 0 0 L -50 100 L -70 100 Z" fill="url(#discoBeam2)" />
        <path d="M 0 0 L -30 -100 L -50 -100 Z" fill="url(#discoBeam3)" />
        
        <line x1="0" y1="-80" x2="0" y2="-45" stroke="#64748b" strokeWidth="3" />
        <circle cx="0" cy="-45" r="3" fill="#cbd5e1" />
        
        <circle cx="0" cy="0" r="45" fill="url(#discoGrad)" stroke="rgba(255,255,255,0.3)" strokeWidth="1.5" />
        <g stroke="rgba(0,0,0,0.2)" strokeWidth="1">
          <path d="M -45 0 C -45 -30, 45 -30, 45 0 C 45 30, -45 30, -45 0 Z" fill="none" />
          <path d="M -38 -25 C -38 -45, 38 -45, 38 -25 C 38 -5, -38 -5, -38 -25 Z" fill="none" />
          <path d="M -38 25 C -38 5, 38 5, 38 25 C 38 45, -38 45, -38 25 Z" fill="none" />
          <path d="M 0 -45 C -25 -45, -25 45, 0 45 C 25 45, 25 -45, 0 -45 Z" fill="none" />
          <path d="M -20 -40 C -35 -40, -35 40, -20 40 C -5 40, -5 -40, -20 -40 Z" fill="none" />
          <path d="M 20 -40 C 35 -40, 35 40, 20 40 C 5 40, 5 -40, 20 -40 Z" fill="none" />
        </g>
        
        <rect x="-10" y="-15" width="6" height="6" fill="#f472b6" opacity="0.8" />
        <rect x="15" y="10" width="8" height="8" fill="#38bdf8" opacity="0.8" />
        <rect x="-25" y="5" width="5" height="7" fill="#a7f3d0" opacity="0.8" />
        <rect x="5" y="-25" width="7" height="5" fill="#fcd34d" opacity="0.8" />
        <circle cx="-15" cy="-10" r="8" fill="#fff" opacity="0.4" filter="blur(2px)" />
      </g>
      
      <g transform="translate(20, -45)" filter="url(#discoBadgeShadow)">
        <rect width="36" height="36" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#f472b6" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 4 14 L 4 2 L 14 0 L 14 12" />
          <circle cx="3" cy="14" r="2" fill="#f472b6" />
          <circle cx="13" cy="12" r="2" fill="#f472b6" />
        </g>
      </g>
    </g>
  </svg>
);

const LavaLampWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="lavaBase" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#94a3b8" />
        <stop offset="100%" stopColor="#334155" />
      </linearGradient>
      <linearGradient id="lavaGlass" x1="0" y1="0" x2="1" y2="0">
        <stop offset="0%" stopColor="rgba(255,255,255,0.1)" />
        <stop offset="50%" stopColor="rgba(255,255,255,0.3)" />
        <stop offset="100%" stopColor="rgba(255,255,255,0.1)" />
      </linearGradient>
      <linearGradient id="lavaLiquid" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#ec4899" opacity="0.8" />
        <stop offset="100%" stopColor="#f43f5e" opacity="0.8" />
      </linearGradient>
      <filter id="lavaShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="lavaBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#lavaShadow)">
        <path d="M -20 35 L 20 35 L 30 75 L -30 75 Z" fill="url(#lavaBase)" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" strokeLinejoin="round" />
        <ellipse cx="0" cy="75" rx="30" ry="8" fill="#334155" stroke="#1e293b" strokeWidth="1" />
        
        <path d="M -15 -45 C -15 -45, -20 10, -20 35 L 20 35 C 20 10, 15 -45, 15 -45 Z" fill="url(#lavaLiquid)" />
        
        <path d="M -15 -45 C -15 -45, -20 10, -20 35 L 20 35 C 20 10, 15 -45, 15 -45 Z" fill="url(#lavaGlass)" stroke="rgba(255,255,255,0.3)" strokeWidth="1.5" />
        
        <circle cx="0" cy="15" r="10" fill="#fcd34d" opacity="0.9" />
        <circle cx="-5" cy="-5" r="6" fill="#fcd34d" opacity="0.9" />
        <circle cx="4" cy="-25" r="8" fill="#fcd34d" opacity="0.9" />
        <path d="M -10 25 C -10 15, 10 15, 10 25 Z" fill="#fcd34d" opacity="0.9" />
        
        <path d="M -10 -65 L 10 -65 L 15 -45 L -15 -45 Z" fill="url(#lavaBase)" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" strokeLinejoin="round" />
        <ellipse cx="0" cy="-65" rx="10" ry="3" fill="#94a3b8" />
        
        <path d="M -8 -20 C -8 10, 0 20, -5 30" fill="none" stroke="#fff" strokeWidth="2" strokeLinecap="round" opacity="0.5" />
      </g>
      
      <g transform="translate(-40, -20)" filter="url(#lavaBadgeShadow)">
        <rect width="36" height="36" rx="10" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(9, 9)" fill="none" stroke="#fcd34d" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="9" cy="9" r="6" />
          <path d="M 9 3 L 9 15 M 3 9 L 15 9 M 5 5 L 13 13 M 5 13 L 13 5" opacity="0.4" />
        </g>
      </g>
    </g>
  </svg>
);

const SunSundialWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="dialBrass" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#fef08a" />
        <stop offset="40%" stopColor="#d97706" />
        <stop offset="80%" stopColor="#78350f" />
        <stop offset="100%" stopColor="#451a03" />
      </linearGradient>
      <filter id="dialShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="15" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 130)">
      <g filter="url(#dialShadow)">
        {/* Pedestal */}
        <path d="M -50 0 L 50 0 L 60 50 L -60 50 Z" fill="#d1d5db" />
        <path d="M -50 0 L 50 0 L 60 50 L -60 50 Z" fill="url(#dialBrass)" opacity="0.2" />
        <ellipse cx="0" cy="50" rx="60" ry="15" fill="#9ca3af" />
        
        {/* Dial Face */}
        <ellipse cx="0" cy="0" rx="80" ry="30" fill="url(#dialBrass)" />
        <ellipse cx="0" cy="-2" rx="80" ry="30" fill="url(#dialBrass)" />
        
        <g stroke="#78350f" opacity="0.6">
          <ellipse cx="0" cy="-2" rx="72" ry="27" fill="none" strokeWidth="2" />
          <ellipse cx="0" cy="-2" rx="60" ry="22" fill="none" strokeWidth="1" />
          
          {/* Dial Marks (approximate radial lines flattened) */}
          <path d="M -60 -2 L -72 -2 M 60 -2 L 72 -2 M 0 -24 L 0 -29 M 0 20 L 0 25 M -42 -17 L -51 -20 M 42 -17 L 51 -20 M -42 13 L -51 16 M 42 13 L 51 16 M -25 -22 L -30 -26 M 25 -22 L 30 -26 M -25 18 L -30 22 M 25 18 L 30 22" strokeWidth="2" strokeLinecap="round" />
        </g>
        
        {/* Gnomon Shadow */}
        <path d="M 0 -2 L -45 -15 L 20 -2 Z" fill="#000" opacity="0.4" />
        
        {/* Gnomon */}
        <path d="M 0 -2 L 0 -60 L 25 -2 Z" fill="url(#dialBrass)" />
        <path d="M 0 -2 L 0 -60 L 25 -2 Z" fill="#000" opacity="0.1" />
        <path d="M 0 -2 L 0 -60 L 25 -2" fill="none" stroke="#fef08a" strokeWidth="1.5" opacity="0.7" />
        <path d="M 0 -60 Q -10 -30 0 -2 Z" fill="url(#dialBrass)" opacity="0.8" />
        
        {/* Small compass at base of gnomon */}
        <circle cx="0" cy="-2" r="6" fill="#451a03" />
        <circle cx="0" cy="-2" r="3" fill="#fef08a" />
      </g>
    </g>
  </svg>
);

const SunOrreryWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="orreryBrass" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#fde047" />
        <stop offset="50%" stopColor="#b45309" />
        <stop offset="100%" stopColor="#451a03" />
      </linearGradient>
      <radialGradient id="sunGlowSphere" cx="40%" cy="40%" r="60%">
        <stop offset="0%" stopColor="#ffffff" />
        <stop offset="30%" stopColor="#fde047" />
        <stop offset="80%" stopColor="#ea580c" />
        <stop offset="100%" stopColor="#7c2d12" />
      </radialGradient>
      <filter id="orreryShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="15" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 140)">
      <g filter="url(#orreryShadow)">
        {/* Base */}
        <ellipse cx="0" cy="50" rx="45" ry="15" fill="url(#orreryBrass)" />
        <path d="M -45 50 L -40 30 L 40 30 L 45 50 Z" fill="url(#orreryBrass)" />
        <ellipse cx="0" cy="30" rx="40" ry="12" fill="#78350f" />
        <rect x="-10" y="10" width="20" height="20" fill="url(#orreryBrass)" />
        <ellipse cx="0" cy="10" rx="35" ry="10" fill="url(#orreryBrass)" />
        
        {/* Central Axis */}
        <line x1="0" y1="10" x2="0" y2="-70" stroke="url(#orreryBrass)" strokeWidth="6" strokeLinecap="round" />
        
        {/* Orbital Rings (Back half) */}
        <path d="M -70 -20 A 70 20 0 0 1 70 -20" fill="none" stroke="url(#orreryBrass)" strokeWidth="2" opacity="0.6" />
        <path d="M -50 -40 A 50 15 0 0 1 50 -40" fill="none" stroke="url(#orreryBrass)" strokeWidth="2" opacity="0.6" />
        <path d="M -90 0 A 90 25 0 0 1 90 0" fill="none" stroke="url(#orreryBrass)" strokeWidth="2" opacity="0.6" />

        {/* Central Sun */}
        <circle cx="0" cy="-50" r="18" fill="url(#sunGlowSphere)" filter="drop-shadow(0 0 8px #f59e0b)" />
        
        {/* Orbital Rings (Front half) */}
        <path d="M -50 -40 A 50 15 0 0 0 50 -40" fill="none" stroke="url(#orreryBrass)" strokeWidth="2" />
        <path d="M -70 -20 A 70 20 0 0 0 70 -20" fill="none" stroke="url(#orreryBrass)" strokeWidth="2" />
        <path d="M -90 0 A 90 25 0 0 0 90 0" fill="none" stroke="url(#orreryBrass)" strokeWidth="2" />

        {/* Planets */}
        <g transform="translate(40, -32)">
          <line x1="0" y1="0" x2="0" y2="-10" stroke="url(#orreryBrass)" strokeWidth="2" />
          <circle cx="0" cy="-10" r="4" fill="#94a3b8" />
        </g>
        <g transform="translate(-60, -10)">
          <line x1="0" y1="0" x2="0" y2="-15" stroke="url(#orreryBrass)" strokeWidth="2" />
          <circle cx="0" cy="-15" r="7" fill="#38bdf8" />
          {/* Moon */}
          <line x1="0" y1="-15" x2="-8" y2="-12" stroke="url(#orreryBrass)" strokeWidth="1" />
          <circle cx="-8" cy="-12" r="2" fill="#e2e8f0" />
        </g>
        <g transform="translate(80, 12)">
          <line x1="0" y1="0" x2="0" y2="-20" stroke="url(#orreryBrass)" strokeWidth="2" />
          <circle cx="0" cy="-20" r="5" fill="#ef4444" />
        </g>
        
        <circle cx="0" cy="-70" r="4" fill="url(#orreryBrass)" />
      </g>
    </g>
  </svg>
);

const SkyAstrolabeWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="astroBrass" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#fef08a" />
        <stop offset="40%" stopColor="#d97706" />
        <stop offset="80%" stopColor="#78350f" />
        <stop offset="100%" stopColor="#451a03" />
      </linearGradient>
      <filter id="astroShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="15" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 130)" filter="url(#astroShadow)">
      {/* Throne (top handle piece) */}
      <path d="M -20 -85 C -20 -100, 20 -100, 20 -85 L 30 -70 L -30 -70 Z" fill="url(#astroBrass)" />
      <circle cx="0" cy="-95" r="8" fill="none" stroke="url(#astroBrass)" strokeWidth="4" />
      
      {/* Mater (main plate) */}
      <circle cx="0" cy="0" r="80" fill="#1f2937" stroke="url(#astroBrass)" strokeWidth="8" />
      <circle cx="0" cy="0" r="76" fill="none" stroke="#78350f" strokeWidth="2" />
      <circle cx="0" cy="0" r="70" fill="none" stroke="#78350f" strokeWidth="1" opacity="0.5" />
      <circle cx="0" cy="0" r="60" fill="none" stroke="#78350f" strokeWidth="1" opacity="0.5" />
      
      {/* Rete (intricate star map cutout) */}
      <g fill="none" stroke="url(#astroBrass)" strokeWidth="3" opacity="0.9">
        <circle cx="0" cy="-20" r="40" />
        <path d="M -40 -20 A 40 40 0 0 1 40 -20" strokeWidth="4" />
        <path d="M -70 0 Q 0 -50 70 0 Q 0 50 -70 0" />
        <path d="M 0 -70 Q 30 -20 0 0 Q -30 -20 0 -70" />
        
        {/* Star pointers */}
        <path d="M 40 -20 L 55 -45 L 45 -10" fill="url(#astroBrass)" stroke="none" />
        <path d="M -40 -20 L -60 -35 L -35 -5" fill="url(#astroBrass)" stroke="none" />
        <path d="M 35 25 L 50 50 L 25 35" fill="url(#astroBrass)" stroke="none" />
        <path d="M -30 20 L -45 50 L -20 30" fill="url(#astroBrass)" stroke="none" />
      </g>
      
      {/* Alidade (rotating rule) */}
      <g transform="rotate(35)">
        <rect x="-85" y="-4" width="170" height="8" rx="2" fill="url(#astroBrass)" />
        <rect x="-85" y="-4" width="170" height="4" fill="#fef08a" opacity="0.4" />
        <circle cx="0" cy="0" r="10" fill="#451a03" stroke="url(#astroBrass)" strokeWidth="2" />
        <circle cx="0" cy="0" r="4" fill="url(#astroBrass)" />
        <polygon points="-75,4 -85,12 -65,12" fill="url(#astroBrass)" />
        <polygon points="75,4 85,12 65,12" fill="url(#astroBrass)" />
      </g>
    </g>
  </svg>
);

const SkySextantWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="sextantBrass" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#fef08a" />
        <stop offset="50%" stopColor="#b45309" />
        <stop offset="100%" stopColor="#451a03" />
      </linearGradient>
      <linearGradient id="sextantMirror" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#e0f2fe" />
        <stop offset="100%" stopColor="#0ea5e9" />
      </linearGradient>
      <filter id="sextantShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="15" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 110)" filter="url(#sextantShadow)">
      {/* Main Frame (Pie slice shape) */}
      <path d="M 0 -60 L -70 60 A 90 90 0 0 0 70 60 Z" fill="none" stroke="url(#sextantBrass)" strokeWidth="10" strokeLinejoin="round" />
      <path d="M 0 -60 L -70 60 A 90 90 0 0 0 70 60 Z" fill="none" stroke="#451a03" strokeWidth="2" opacity="0.4" strokeLinejoin="round" />
      
      {/* Frame webbing / crossbars */}
      <line x1="0" y1="-60" x2="0" y2="90" stroke="url(#sextantBrass)" strokeWidth="8" />
      <line x1="-35" y1="0" x2="35" y2="0" stroke="url(#sextantBrass)" strokeWidth="8" />
      <circle cx="0" cy="15" r="25" fill="none" stroke="url(#sextantBrass)" strokeWidth="6" />
      
      {/* Arc Scale */}
      <path d="M -75 65 A 95 95 0 0 0 75 65" fill="none" stroke="#d1d5db" strokeWidth="8" />
      <path d="M -75 65 A 95 95 0 0 0 75 65" fill="none" stroke="#000" strokeWidth="1" strokeDasharray="3 3" opacity="0.5" />
      
      {/* Telescope Tube */}
      <g transform="rotate(-30) translate(-30, 20)">
        <rect x="-10" y="-8" width="50" height="16" rx="4" fill="url(#sextantBrass)" />
        <rect x="40" y="-12" width="15" height="24" rx="2" fill="#1f2937" />
        <rect x="-20" y="-10" width="10" height="20" rx="2" fill="url(#sextantBrass)" />
      </g>
      
      {/* Index Mirror (Top) */}
      <rect x="-10" y="-68" width="20" height="16" fill="url(#sextantBrass)" />
      <rect x="-6" y="-66" width="12" height="12" fill="url(#sextantMirror)" />
      
      {/* Horizon Mirror & Shades */}
      <g transform="translate(-40, -10)">
        <rect x="-10" y="-10" width="20" height="20" fill="url(#sextantBrass)" />
        <rect x="-8" y="-8" width="16" height="8" fill="url(#sextantMirror)" />
        
        {/* Colored Shades */}
        <rect x="15" y="-15" width="8" height="25" fill="#ef4444" opacity="0.6" stroke="url(#sextantBrass)" strokeWidth="2" />
        <rect x="25" y="-10" width="8" height="25" fill="#3b82f6" opacity="0.6" stroke="url(#sextantBrass)" strokeWidth="2" />
      </g>
      
      {/* Index Arm */}
      <g transform="rotate(15)">
        <line x1="0" y1="-60" x2="0" y2="105" stroke="url(#sextantBrass)" strokeWidth="10" strokeLinecap="round" />
        <line x1="0" y1="-60" x2="0" y2="105" stroke="#451a03" strokeWidth="2" opacity="0.4" strokeLinecap="round" />
        {/* Vernier Scale / Micrometer drum at bottom */}
        <rect x="-15" y="95" width="30" height="15" rx="3" fill="url(#sextantBrass)" />
        <rect x="-8" y="110" width="16" height="10" rx="2" fill="#374151" />
        <circle cx="0" cy="-60" r="6" fill="#1f2937" />
      </g>
    </g>
  </svg>
);

const AtmosphereBarometerWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="baroWood" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#7c2d12" />
        <stop offset="50%" stopColor="#451a03" />
        <stop offset="100%" stopColor="#2c0a00" />
      </linearGradient>
      <linearGradient id="baroBrass" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#fef08a" />
        <stop offset="50%" stopColor="#b45309" />
        <stop offset="100%" stopColor="#451a03" />
      </linearGradient>
      <filter id="baroShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="15" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <radialGradient id="baroGlass" cx="30%" cy="30%" r="70%">
        <stop offset="0%" stopColor="#ffffff" stopOpacity="0.6" />
        <stop offset="50%" stopColor="#ffffff" stopOpacity="0.1" />
        <stop offset="100%" stopColor="#000000" stopOpacity="0.3" />
      </radialGradient>
    </defs>
    <g transform="translate(120, 120)" filter="url(#baroShadow)">
      {/* Wood Base */}
      <circle cx="0" cy="0" r="95" fill="url(#baroWood)" />
      <circle cx="0" cy="0" r="88" fill="none" stroke="#2c0a00" strokeWidth="4" />
      
      {/* Brass Bezel */}
      <circle cx="0" cy="0" r="75" fill="url(#baroBrass)" />
      <circle cx="0" cy="0" r="68" fill="#f8fafc" />
      
      {/* Dial Face */}
      <circle cx="0" cy="0" r="68" fill="none" stroke="#94a3b8" strokeWidth="1" />
      <circle cx="0" cy="0" r="55" fill="none" stroke="#cbd5e1" strokeWidth="1" />
      
      {/* Dial Ticks */}
      <g stroke="#334155" strokeWidth="2">
        {[...Array(24)].map((_, i) => (
          <line key={i} x1="0" y1="-68" x2="0" y2="-62" transform={`rotate(${i * 15 - 120})`} />
        ))}
      </g>
      <g stroke="#94a3b8" strokeWidth="1">
        {[...Array(72)].map((_, i) => (
          i % 3 !== 0 ? <line key={i} x1="0" y1="-68" x2="0" y2="-64" transform={`rotate(${i * 5 - 120})`} /> : null
        ))}
      </g>
      
      {/* Decorative Text Marks */}
      <text x="-40" y="-10" fontSize="10" fontFamily="serif" fill="#1e293b" transform="rotate(-30, -40, -10)" textAnchor="middle" fontWeight="bold">RAIN</text>
      <text x="0" y="-35" fontSize="10" fontFamily="serif" fill="#1e293b" textAnchor="middle" fontWeight="bold">CHANGE</text>
      <text x="40" y="-10" fontSize="10" fontFamily="serif" fill="#1e293b" transform="rotate(30, 40, -10)" textAnchor="middle" fontWeight="bold">FAIR</text>
      
      <text x="0" y="30" fontSize="8" fontFamily="serif" fill="#64748b" textAnchor="middle">ANEROID</text>
      <text x="0" y="42" fontSize="8" fontFamily="serif" fill="#64748b" textAnchor="middle">BAROMETER</text>

      {/* Mechanism Window (exposing internal gears) */}
      <circle cx="0" cy="10" r="18" fill="#e2e8f0" stroke="#cbd5e1" strokeWidth="2" />
      <circle cx="-5" cy="5" r="8" fill="none" stroke="url(#baroBrass)" strokeWidth="3" strokeDasharray="2 2" />
      <circle cx="5" cy="15" r="6" fill="none" stroke="#94a3b8" strokeWidth="2" strokeDasharray="1.5 1.5" />
      <circle cx="0" cy="10" r="18" fill="url(#baroGlass)" opacity="0.5" />

      {/* Gold Tracking Hand */}
      <g transform="rotate(45)">
        <path d="M -2 15 L 0 -55 L 2 15 Z" fill="url(#baroBrass)" />
        <circle cx="0" cy="0" r="6" fill="url(#baroBrass)" />
      </g>
      
      {/* Black Indicator Hand */}
      <g transform="rotate(-15)">
        <path d="M -3 20 L 0 -60 L 3 20 Z" fill="#0f172a" />
        <path d="M -1 20 L 0 -60 L 1 20 Z" fill="#334155" />
        <circle cx="0" cy="0" r="4" fill="#0f172a" />
        <circle cx="0" cy="-55" r="3" fill="#0f172a" />
      </g>
      
      {/* Center Pin */}
      <circle cx="0" cy="0" r="3" fill="#fef08a" />
      
      {/* Top Hinge/Ring */}
      <path d="M -15 -95 C -15 -115, 15 -115, 15 -95" fill="none" stroke="url(#baroBrass)" strokeWidth="6" />
      <circle cx="0" cy="-95" r="8" fill="url(#baroBrass)" />

      {/* Domed Glass over face */}
      <circle cx="0" cy="0" r="68" fill="url(#baroGlass)" />
      
      {/* Glass Reflection Arc */}
      <path d="M -50 -40 A 60 60 0 0 1 50 -40 A 55 55 0 0 0 -50 -40 Z" fill="#fff" opacity="0.3" />
    </g>
  </svg>
);

const AtmosphereAnemometerWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="anemoMetal" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#f3f4f6" />
        <stop offset="50%" stopColor="#9ca3af" />
        <stop offset="100%" stopColor="#374151" />
      </linearGradient>
      <linearGradient id="anemoDark" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#374151" />
        <stop offset="50%" stopColor="#111827" />
        <stop offset="100%" stopColor="#000000" />
      </linearGradient>
      <linearGradient id="anemoRed" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#fca5a5" />
        <stop offset="50%" stopColor="#dc2626" />
        <stop offset="100%" stopColor="#7f1d1d" />
      </linearGradient>
      <filter id="anemoShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="15" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 130)" filter="url(#anemoShadow)">
      {/* Base & Mast */}
      <path d="M -30 90 L 30 90 L 20 70 L -20 70 Z" fill="url(#anemoDark)" />
      <rect x="-25" y="85" width="50" height="5" fill="#111827" />
      <path d="M -20 70 L 20 70 L 10 30 L -10 30 Z" fill="url(#anemoMetal)" />
      
      {/* Sensor housing */}
      <rect x="-15" y="10" width="30" height="20" rx="4" fill="url(#anemoDark)" />
      <circle cx="0" cy="20" r="6" fill="#ef4444" opacity="0.8" />
      
      <line x1="0" y1="10" x2="0" y2="-60" stroke="url(#anemoMetal)" strokeWidth="8" strokeLinecap="round" />
      <line x1="-3" y1="10" x2="-3" y2="-60" stroke="#fff" strokeWidth="2" opacity="0.3" strokeLinecap="round" />

      {/* Rotating Hub */}
      <circle cx="0" cy="-60" r="12" fill="url(#anemoDark)" />
      <circle cx="0" cy="-60" r="6" fill="url(#anemoMetal)" />
      
      {/* Rotor Arms and Cups */}
      {/* Left Cup (Back) */}
      <g transform="translate(0, -60)">
        <line x1="0" y1="0" x2="-55" y2="-10" stroke="url(#anemoMetal)" strokeWidth="4" />
        <path d="M -55 -10 A 15 15 0 0 1 -75 -25 L -65 -5 A 15 15 0 0 0 -55 -10 Z" fill="url(#anemoDark)" />
        <ellipse cx="-65" cy="-15" rx="12" ry="18" fill="url(#anemoDark)" transform="rotate(30 -65 -15)" />
      </g>
      
      {/* Right Cup (Red - Indicator) */}
      <g transform="translate(0, -60)">
        <line x1="0" y1="0" x2="65" y2="0" stroke="url(#anemoMetal)" strokeWidth="4" />
        <ellipse cx="75" cy="0" rx="10" ry="18" fill="url(#anemoRed)" />
        <ellipse cx="70" cy="0" rx="4" ry="15" fill="#7f1d1d" />
      </g>

      {/* Front Cup */}
      <g transform="translate(0, -60)">
        <line x1="0" y1="0" x2="-20" y2="35" stroke="url(#anemoMetal)" strokeWidth="4" />
        <ellipse cx="-25" cy="45" rx="16" ry="12" fill="url(#anemoDark)" />
        <ellipse cx="-25" cy="40" rx="14" ry="6" fill="#111827" />
        <path d="M -39 40 A 14 6 0 0 0 -11 40" fill="none" stroke="#fff" strokeWidth="1" opacity="0.2" />
      </g>
    </g>
  </svg>
);

const ModernSunsetWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="sunsetSky" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#4f46e5" />
        <stop offset="30%" stopColor="#ec4899" />
        <stop offset="70%" stopColor="#f59e0b" />
        <stop offset="100%" stopColor="#fde047" />
      </linearGradient>
      <linearGradient id="sunsetOcean" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#1e3a8a" />
        <stop offset="50%" stopColor="#312e81" />
        <stop offset="100%" stopColor="#0f172a" />
      </linearGradient>
      <linearGradient id="sunGradient" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#fde047" />
        <stop offset="100%" stopColor="#ea580c" />
      </linearGradient>
      <filter id="sunGlow">
        <feDropShadow dx="0" dy="0" stdDeviation="15" floodColor="#f59e0b" floodOpacity="0.8" />
      </filter>
      <clipPath id="sunsetClip">
        <rect width="140" height="140" rx="24" />
      </clipPath>
      <filter id="sunsetCardShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="20" floodColor="#ec4899" floodOpacity="0.3" />
      </filter>
    </defs>
    <g transform="translate(50, 50)" filter="url(#sunsetCardShadow)">
      {/* Card Base */}
      <rect width="140" height="140" rx="24" fill="#0f172a" stroke="#334155" strokeWidth="2" />
      
      <g clipPath="url(#sunsetClip)">
        {/* Sky Background */}
        <rect width="140" height="90" fill="url(#sunsetSky)" />
        
        {/* Sun */}
        <circle cx="70" cy="80" r="28" fill="url(#sunGradient)" filter="url(#sunGlow)" />
        
        {/* Water / Ocean */}
        <rect x="0" y="90" width="140" height="50" fill="url(#sunsetOcean)" />
        
        {/* Sun Reflections on Water */}
        <path d="M 50 95 L 90 95 M 55 102 L 85 102 M 62 109 L 78 109 M 66 116 L 74 116" stroke="#fde047" strokeWidth="2.5" strokeLinecap="round" opacity="0.6" />
        
        {/* Modern decorative framing lines */}
        <line x1="20" y1="20" x2="40" y2="20" stroke="#fff" strokeWidth="3" strokeLinecap="round" opacity="0.4" />
        <circle cx="120" cy="20" r="4" fill="#fff" opacity="0.4" />
      </g>
      
      {/* Outer Glow / Glass Border */}
      <rect width="140" height="140" rx="24" fill="none" stroke="#fff" strokeWidth="1.5" opacity="0.2" />
    </g>
  </svg>
);

const ModernCloudscapeWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="cloudSky" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#bae6fd" />
        <stop offset="100%" stopColor="#3b82f6" />
      </linearGradient>
      <linearGradient id="cloudWhite" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#ffffff" />
        <stop offset="100%" stopColor="#e0f2fe" />
      </linearGradient>
      <linearGradient id="cloudShadow" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#f1f5f9" />
        <stop offset="100%" stopColor="#94a3b8" />
      </linearGradient>
      <filter id="cloudSoftShadow">
        <feDropShadow dx="0" dy="8" stdDeviation="12" floodColor="#0f172a" floodOpacity="0.25" />
      </filter>
      <clipPath id="cloudClip">
        <rect width="140" height="140" rx="24" />
      </clipPath>
      <filter id="cloudCardShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="20" floodColor="#3b82f6" floodOpacity="0.3" />
      </filter>
    </defs>
    <g transform="translate(50, 50)" filter="url(#cloudCardShadow)">
      <rect width="140" height="140" rx="24" fill="url(#cloudSky)" stroke="#60a5fa" strokeWidth="2" />
      
      <g clipPath="url(#cloudClip)">
        {/* Background Clouds */}
        <g opacity="0.5" transform="translate(10, 20)">
          <path d="M 20 50 A 15 15 0 0 1 45 40 A 20 20 0 0 1 80 45 A 15 15 0 0 1 100 50 Z" fill="url(#cloudWhite)" />
        </g>
        <g opacity="0.4" transform="translate(-10, 80) scale(0.8)">
          <path d="M 20 50 A 15 15 0 0 1 45 40 A 20 20 0 0 1 80 45 A 15 15 0 0 1 100 50 Z" fill="url(#cloudWhite)" />
        </g>
        
        {/* Main Fluffy Cloud */}
        <g transform="translate(25, 45)" filter="url(#cloudSoftShadow)">
          {/* Back shadow layer for depth */}
          <path d="M 20 50 A 18 18 0 0 1 45 35 A 25 25 0 0 1 85 40 A 20 20 0 0 1 105 50 Z" fill="url(#cloudShadow)" transform="translate(0, 4)" />
          {/* Front white layer */}
          <path d="M 15 50 A 18 18 0 0 1 40 35 A 25 25 0 0 1 80 40 A 20 20 0 0 1 100 50 Z" fill="url(#cloudWhite)" />
          
          {/* Subtle highlights */}
          <path d="M 40 35 A 25 25 0 0 1 70 38" fill="none" stroke="#fff" strokeWidth="3" strokeLinecap="round" opacity="0.8" />
        </g>
        
        {/* Modern UI elements */}
        <rect x="20" y="110" width="35" height="6" rx="3" fill="#fff" opacity="0.8" />
        <rect x="20" y="120" width="60" height="6" rx="3" fill="#fff" opacity="0.4" />
      </g>
      
      <rect width="140" height="140" rx="24" fill="none" stroke="#fff" strokeWidth="1.5" opacity="0.4" />
    </g>
  </svg>
);

const ModernAuroraAtmosphereWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="auroraBg" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#020617" />
        <stop offset="100%" stopColor="#0f172a" />
      </linearGradient>
      <linearGradient id="auroraWave1" x1="0" y1="0" x2="1" y2="0">
        <stop offset="0%" stopColor="#10b981" stopOpacity="0" />
        <stop offset="50%" stopColor="#10b981" stopOpacity="0.8" />
        <stop offset="100%" stopColor="#3b82f6" stopOpacity="0" />
      </linearGradient>
      <linearGradient id="auroraWave2" x1="0" y1="0" x2="1" y2="0">
        <stop offset="0%" stopColor="#8b5cf6" stopOpacity="0" />
        <stop offset="50%" stopColor="#c084fc" stopOpacity="0.8" />
        <stop offset="100%" stopColor="#ec4899" stopOpacity="0" />
      </linearGradient>
      <filter id="auroraGlow">
        <feGaussianBlur stdDeviation="8" result="blur" />
        <feComposite in="SourceGraphic" in2="blur" operator="over" />
      </filter>
      <clipPath id="auroraClip">
        <rect width="140" height="140" rx="24" />
      </clipPath>
      <filter id="auroraCardShadow">
        <feDropShadow dx="0" dy="15" stdDeviation="20" floodColor="#8b5cf6" floodOpacity="0.25" />
      </filter>
    </defs>
    <g transform="translate(50, 50)" filter="url(#auroraCardShadow)">
      <rect width="140" height="140" rx="24" fill="url(#auroraBg)" stroke="#1e293b" strokeWidth="2" />
      
      <g clipPath="url(#auroraClip)">
        {/* Stars */}
        <circle cx="20" cy="30" r="1.5" fill="#fff" opacity="0.6" />
        <circle cx="80" cy="15" r="1" fill="#fff" opacity="0.4" />
        <circle cx="120" cy="40" r="2" fill="#fff" opacity="0.8" />
        <circle cx="40" cy="70" r="1" fill="#fff" opacity="0.3" />
        <circle cx="100" cy="90" r="1.5" fill="#fff" opacity="0.7" />
        
        {/* Aurora Waves */}
        <g filter="url(#auroraGlow)">
          <path d="M -20 80 C 20 60, 60 100, 100 60 C 130 40, 160 50, 160 50" fill="none" stroke="url(#auroraWave1)" strokeWidth="15" strokeLinecap="round" />
          <path d="M -20 110 C 30 110, 50 60, 90 70 C 130 80, 160 50, 160 50" fill="none" stroke="url(#auroraWave2)" strokeWidth="12" strokeLinecap="round" opacity="0.7" />
        </g>
        
        {/* Modern UI accents */}
        <circle cx="70" cy="70" r="45" fill="none" stroke="#fff" strokeWidth="1" opacity="0.1" />
        <circle cx="70" cy="70" r="30" fill="none" stroke="#fff" strokeWidth="1" strokeDasharray="4 4" opacity="0.15" />
        
        <polygon points="70,60 80,75 60,75" fill="#fff" opacity="0.4" />
      </g>
      
      <rect width="140" height="140" rx="24" fill="none" stroke="#475569" strokeWidth="1.5" opacity="0.5" />
    </g>
  </svg>
);

const MeshWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="meshFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="meshShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="meshBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <linearGradient id="meshWireframe" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#38bdf8" />
        <stop offset="100%" stopColor="#818cf8" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 110)">
      <g filter="url(#meshShadow)">
        {/* Base Platform */}
        <path d="M -50 40 L 0 60 L 50 40 L 0 20 Z" fill="#111" stroke="#333" strokeWidth="1.5" />
        <path d="M -50 40 L 0 60 L 0 75 L -50 55 Z" fill="url(#meshFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <path d="M 0 60 L 50 40 L 50 55 L 0 75 Z" fill="url(#meshFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* 3D Icosahedron / Crystal shape */}
        <g stroke="url(#meshWireframe)" strokeWidth="2" strokeLinejoin="round">
          {/* Back faces */}
          <path d="M 0 -40 L -30 -10 L 0 30 Z" fill="#111" opacity="0.6" />
          <path d="M 0 -40 L 30 -10 L 0 30 Z" fill="#111" opacity="0.4" />
          
          {/* Front faces */}
          <path d="M 0 -40 L -20 5 L 0 30 Z" fill="#2d2d2d" opacity="0.8" />
          <path d="M 0 -40 L 20 5 L 0 30 Z" fill="#363636" opacity="0.9" />
          <path d="M -20 5 L 20 5 L 0 30 Z" fill="#1f1f1f" />
          <path d="M -30 -10 L -20 5 L 0 -40 Z" fill="#1f1f1f" opacity="0.5" />
          <path d="M 30 -10 L 20 5 L 0 -40 Z" fill="#2d2d2d" opacity="0.5" />
        </g>
        
        {/* Vertices */}
        <circle cx="0" cy="-40" r="3" fill="#38bdf8" />
        <circle cx="-30" cy="-10" r="3" fill="#38bdf8" />
        <circle cx="30" cy="-10" r="3" fill="#38bdf8" />
        <circle cx="-20" cy="5" r="3" fill="#38bdf8" />
        <circle cx="20" cy="5" r="3" fill="#38bdf8" />
        <circle cx="0" cy="30" r="3" fill="#38bdf8" />
      </g>

      {/* Badge */}
      <g transform="translate(30, -30)" filter="url(#meshBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#38bdf8" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
          <polygon points="10,2 18,6 18,14 10,18 2,14 2,6" />
          <line x1="10" y1="2" x2="10" y2="10" />
          <line x1="10" y1="10" x2="18" y2="14" />
          <line x1="10" y1="10" x2="2" y2="14" />
        </g>
      </g>
    </g>
  </svg>
);

const MaterialWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="matFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="matShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="matBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      {/* Checkerboard Pattern */}
      <pattern id="checkerPattern" width="16" height="16" patternUnits="userSpaceOnUse" viewBox="0 0 16 16">
        <rect width="8" height="8" fill="#a855f7" opacity="0.8" />
        <rect x="8" width="8" height="8" fill="#1f2937" />
        <rect y="8" width="8" height="8" fill="#1f2937" />
        <rect x="8" y="8" width="8" height="8" fill="#a855f7" opacity="0.8" />
      </pattern>
      {/* Sphere lighting overlay */}
      <radialGradient id="sphereLight" cx="30%" cy="30%" r="70%">
        <stop offset="0%" stopColor="#fff" stopOpacity="0.6" />
        <stop offset="40%" stopColor="#fff" stopOpacity="0" />
        <stop offset="100%" stopColor="#000" stopOpacity="0.8" />
      </radialGradient>
      <clipPath id="sphereClip">
        <circle cx="0" cy="0" r="40" />
      </clipPath>
    </defs>
    <g transform="translate(120, 110)">
      <g filter="url(#matShadow)">
        {/* Base Pedestal */}
        <ellipse cx="0" cy="45" rx="35" ry="12" fill="#111" stroke="#333" strokeWidth="1" />
        <path d="M -35 45 C -35 55, 35 55, 35 45 L 35 55 C 35 65, -35 65, -35 55 Z" fill="url(#matFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Sphere Support Ring */}
        <ellipse cx="0" cy="40" rx="20" ry="6" fill="#2d2d2d" stroke="rgba(255,255,255,0.1)" strokeWidth="1" />
        
        {/* Material Sphere */}
        <circle cx="0" cy="0" r="40" fill="#1f2937" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" />
        
        {/* 3D Distorted Checkerboard (simulated with standard pattern for now, but curved look with shadow overlay) */}
        <g clipPath="url(#sphereClip)">
          {/* Background pattern */}
          <rect x="-40" y="-40" width="80" height="80" fill="url(#checkerPattern)" />
          {/* Lighting/Shading overlay to make it look spherical */}
          <circle cx="0" cy="0" r="40" fill="url(#sphereLight)" />
        </g>
        <circle cx="0" cy="0" r="40" fill="none" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, -45)" filter="url(#matBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)">
          <circle cx="10" cy="10" r="6" fill="#a855f7" />
          <path d="M 4 10 A 6 6 0 0 1 16 10 Z" fill="#fff" opacity="0.5" />
        </g>
      </g>
    </g>
  </svg>
);

const ParticleWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="partFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="partShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="partBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <radialGradient id="particleGlow" cx="50%" cy="50%" r="50%">
        <stop offset="0%" stopColor="#fb923c" stopOpacity="1" />
        <stop offset="50%" stopColor="#f97316" stopOpacity="0.6" />
        <stop offset="100%" stopColor="#ea580c" stopOpacity="0" />
      </radialGradient>
    </defs>
    <g transform="translate(120, 140)">
      {/* Particles flying up */}
      <g>
        <circle cx="0" cy="-30" r="8" fill="url(#particleGlow)" />
        <circle cx="0" cy="-30" r="3" fill="#fff" />
        
        <circle cx="-25" cy="-60" r="10" fill="url(#particleGlow)" />
        <circle cx="-25" cy="-60" r="4" fill="#fff" />
        
        <circle cx="30" cy="-45" r="6" fill="url(#particleGlow)" />
        <circle cx="30" cy="-45" r="2" fill="#fff" />
        
        <circle cx="15" cy="-80" r="12" fill="url(#particleGlow)" opacity="0.8" />
        <circle cx="15" cy="-80" r="5" fill="#fff" opacity="0.8" />
        
        <circle cx="-10" cy="-95" r="5" fill="url(#particleGlow)" opacity="0.6" />
        <circle cx="-10" cy="-95" r="2" fill="#fff" opacity="0.6" />
        
        {/* Motion lines */}
        <line x1="-15" y1="-20" x2="-23" y2="-45" stroke="#f97316" strokeWidth="1.5" strokeDasharray="2 4" strokeLinecap="round" opacity="0.5" />
        <line x1="15" y1="-15" x2="28" y2="-35" stroke="#f97316" strokeWidth="1.5" strokeDasharray="2 4" strokeLinecap="round" opacity="0.5" />
        <line x1="0" y1="-5" x2="0" y2="-20" stroke="#f97316" strokeWidth="2" strokeDasharray="2 4" strokeLinecap="round" opacity="0.5" />
      </g>

      <g filter="url(#partShadow)">
        {/* Emitter Base */}
        <path d="M -20 -10 L 20 -10 L 30 15 L -30 15 Z" fill="#111" stroke="#333" strokeWidth="1" />
        <path d="M -30 15 L 30 15 L 30 25 L -30 25 Z" fill="url(#partFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Emitter nozzle inner */}
        <ellipse cx="0" cy="-10" rx="20" ry="6" fill="#f97316" opacity="0.2" />
        <ellipse cx="0" cy="-10" rx="14" ry="4" fill="#ea580c" opacity="0.5" />
        <ellipse cx="0" cy="-10" rx="6" ry="2" fill="#fff" />
      </g>

      {/* Badge */}
      <g transform="translate(25, -60)" filter="url(#partBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#f97316" strokeWidth="2" strokeLinecap="round">
          <circle cx="10" cy="10" r="1.5" fill="#f97316" />
          <circle cx="4" cy="5" r="1.5" fill="#f97316" />
          <circle cx="16" cy="6" r="1.5" fill="#f97316" />
          <circle cx="7" cy="16" r="1.5" fill="#f97316" />
          <circle cx="14" cy="15" r="1.5" fill="#f97316" />
        </g>
      </g>
    </g>
  </svg>
);

const RiggingWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="rigFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="rigShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="rigBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#rigShadow)">
        {/* Bone connecting shapes */}
        <g transform="rotate(-15)">
          {/* Bottom Bone */}
          <path d="M 0 50 L -12 25 L 0 0 L 12 25 Z" fill="#2d2d2d" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" strokeLinejoin="round" />
          <path d="M 0 50 L 0 0 M 0 0 L 12 25" fill="none" stroke="rgba(255,255,255,0.05)" strokeWidth="1.5" />
          
          {/* Top Bone */}
          <path d="M 0 0 L -15 -30 L 0 -60 L 15 -30 Z" fill="url(#rigFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" strokeLinejoin="round" />
          <path d="M 0 0 L 0 -60 M 0 -60 L 15 -30" fill="none" stroke="rgba(255,255,255,0.1)" strokeWidth="1.5" />
          
          {/* Joints */}
          {/* Root Joint */}
          <circle cx="0" cy="50" r="6" fill="#111" stroke="#34d399" strokeWidth="2" />
          <circle cx="0" cy="50" r="2" fill="#34d399" />
          
          {/* Middle Joint */}
          <circle cx="0" cy="0" r="8" fill="#111" stroke="#34d399" strokeWidth="2" />
          <circle cx="0" cy="0" r="3" fill="#34d399" />
          
          {/* End Joint */}
          <circle cx="0" cy="-60" r="5" fill="#111" stroke="#34d399" strokeWidth="2" />
          <circle cx="0" cy="-60" r="1.5" fill="#34d399" />
          
          {/* Rotation arc indicator */}
          <path d="M -15 0 A 15 15 0 0 1 15 0" fill="none" stroke="#34d399" strokeWidth="1.5" strokeDasharray="2 3" />
        </g>
      </g>

      {/* Badge */}
      <g transform="translate(-45, 10)" filter="url(#rigBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#34d399" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="5" cy="15" r="2" />
          <circle cx="15" cy="5" r="2" />
          <line x1="6.5" y1="13.5" x2="13.5" y2="6.5" />
        </g>
      </g>
    </g>
  </svg>
);

const ScriptNodeWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="nodeFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <linearGradient id="nodeHeaderGrad" x1="0" y1="0" x2="1" y2="0">
        <stop offset="0%" stopColor="#3b82f6" />
        <stop offset="100%" stopColor="#2563eb" />
      </linearGradient>
      <filter id="nodeShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="nodeBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      {/* Background connecting wires */}
      <path d="M -80 -10 C -40 -10, -30 20, -35 20" fill="none" stroke="#9ca3af" strokeWidth="3" strokeLinecap="round" opacity="0.5" />
      <path d="M -80 30 C -50 30, -30 40, -35 40" fill="none" stroke="#f43f5e" strokeWidth="3" strokeLinecap="round" opacity="0.6" />
      <path d="M 35 25 C 60 25, 60 -5, 80 -5" fill="none" stroke="#10b981" strokeWidth="3" strokeLinecap="round" opacity="0.6" />

      <g filter="url(#nodeShadow)">
        {/* Main Node Body */}
        <rect x="-45" y="-30" width="90" height="85" rx="8" fill="url(#nodeFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Header */}
        <path d="M -45 -22 C -45 -26.4, -41.4 -30, -37 -30 L 37 -30 C 41.4 -30, 45 -26.4, 45 -22 L 45 -5 L -45 -5 Z" fill="url(#nodeHeaderGrad)" />
        <rect x="-45" y="-30" width="90" height="25" rx="8" fill="none" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" />
        
        {/* Header Text (fake) */}
        <rect x="-35" y="-20" width="35" height="6" rx="3" fill="#fff" opacity="0.9" />
        
        {/* Pins - Inputs (Left) */}
        <circle cx="-45" cy="10" r="5" fill="#9ca3af" stroke="#1f1f1f" strokeWidth="2" />
        <rect x="-35" y="8" width="20" height="4" rx="2" fill="#555" />
        
        <circle cx="-45" cy="30" r="5" fill="#f43f5e" stroke="#1f1f1f" strokeWidth="2" />
        <rect x="-35" y="28" width="12" height="4" rx="2" fill="#555" />
        
        {/* Pins - Outputs (Right) */}
        <circle cx="45" cy="15" r="5" fill="#10b981" stroke="#1f1f1f" strokeWidth="2" />
        <rect x="15" y="13" width="20" height="4" rx="2" fill="#555" />
      </g>

      {/* Badge */}
      <g transform="translate(15, -45)" filter="url(#nodeBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#3b82f6" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="3" y="5" width="14" height="10" rx="2" />
          <line x1="1" y1="8" x2="3" y2="8" />
          <line x1="1" y1="12" x2="3" y2="12" />
          <line x1="17" y1="10" x2="19" y2="10" />
        </g>
      </g>
    </g>
  </svg>
);

const ColliderWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="colFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="colShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="colBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#colShadow)">
        {/* The object (Capsule) */}
        <rect x="-20" y="-35" width="40" height="70" rx="20" fill="url(#colFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="0" cy="-15" r="12" fill="#2d2d2d" stroke="rgba(255,255,255,0.1)" strokeWidth="1" />
        <circle cx="0" cy="15" r="12" fill="#2d2d2d" stroke="rgba(255,255,255,0.1)" strokeWidth="1" />
      </g>

      {/* Physics wireframe bounding box */}
      <g fill="none" stroke="#a855f7" strokeWidth="2" strokeDasharray="4 4" opacity="0.8">
        <rect x="-30" y="-45" width="60" height="90" />
        <circle cx="0" cy="0" r="3" fill="#a855f7" stroke="none" />
        <line x1="-30" y1="-45" x2="30" y2="45" strokeWidth="1" />
        <line x1="-30" y1="45" x2="30" y2="-45" strokeWidth="1" />
      </g>
      
      {/* Bounding box corner markers */}
      <path d="M -30 -35 L -30 -45 L -20 -45" fill="none" stroke="#a855f7" strokeWidth="3" />
      <path d="M 30 -35 L 30 -45 L 20 -45" fill="none" stroke="#a855f7" strokeWidth="3" />
      <path d="M -30 35 L -30 45 L -20 45" fill="none" stroke="#a855f7" strokeWidth="3" />
      <path d="M 30 35 L 30 45 L 20 45" fill="none" stroke="#a855f7" strokeWidth="3" />

      {/* Badge */}
      <g transform="translate(30, -10)" filter="url(#colBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#a855f7" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="3" y="3" width="14" height="14" strokeDasharray="2 2" />
          <circle cx="10" cy="10" r="4" fill="#a855f7" opacity="0.3" stroke="none" />
        </g>
      </g>
    </g>
  </svg>
);

const TerrainWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="terrainFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="terrainShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="terrainBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <linearGradient id="terrainWireframe" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#10b981" />
        <stop offset="100%" stopColor="#059669" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 110)">
      <g filter="url(#terrainShadow)">
        {/* Base Platform */}
        <path d="M -55 25 L 0 45 L 55 25 L 0 5 Z" fill="#111" stroke="#333" strokeWidth="1.5" />
        <path d="M -55 25 L 0 45 L 0 60 L -55 40 Z" fill="url(#terrainFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <path d="M 0 45 L 55 25 L 55 40 L 0 60 Z" fill="url(#terrainFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Terrain Mesh */}
        <g stroke="url(#terrainWireframe)" strokeWidth="1" strokeLinejoin="round" fill="rgba(16, 185, 129, 0.1)">
          <path d="M -55 25 L -35 15 L -20 0 L 0 -20 L 10 -5 L 30 10 L 55 25 L 0 45 Z" />
          <path d="M -35 15 L -10 5 L 0 -10 L 15 5 L 30 10" />
          <path d="M -20 0 L -5 -15 L 10 -25 L 25 -10 L 40 5" />
          <path d="M -45 32 L -20 20 L 5 5 L 25 20 L 45 32" />
        </g>
        
        {/* Wireframe details */}
        <path d="M -20 0 L -20 20 M 0 -20 L 0 45 M 30 10 L 30 25 M -35 15 L -35 30" fill="none" stroke="rgba(16, 185, 129, 0.3)" strokeWidth="0.5" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, -45)" filter="url(#terrainBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#10b981" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 2 16 L 8 6 L 14 16 Z" />
          <path d="M 10 12 L 14 4 L 20 16 Z" />
        </g>
      </g>
    </g>
  </svg>
);

const AudioEmitterWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="audioFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="audioShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="audioBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <radialGradient id="audioWave" cx="50%" cy="50%" r="50%">
        <stop offset="0%" stopColor="#ec4899" stopOpacity="0.8" />
        <stop offset="100%" stopColor="#ec4899" stopOpacity="0" />
      </radialGradient>
    </defs>
    <g transform="translate(120, 110)">
      {/* Sound Waves */}
      <path d="M 20 -30 Q 40 -30, 40 0 Q 40 30, 20 30" fill="none" stroke="#ec4899" strokeWidth="2" strokeLinecap="round" opacity="0.4" />
      <path d="M 35 -45 Q 60 -45, 60 0 Q 60 45, 35 45" fill="none" stroke="#ec4899" strokeWidth="2.5" strokeLinecap="round" opacity="0.6" />
      <path d="M 50 -60 Q 80 -60, 80 0 Q 80 60, 50 60" fill="none" stroke="#ec4899" strokeWidth="3" strokeLinecap="round" opacity="0.8" />
      
      <g filter="url(#audioShadow)">
        {/* Speaker Body */}
        <rect x="-40" y="-30" width="40" height="60" rx="4" fill="url(#audioFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <path d="M 0 -15 L 20 -35 L 20 35 L 0 15 Z" fill="#2d2d2d" stroke="rgba(255,255,255,0.1)" strokeWidth="1.5" strokeLinejoin="round" />
        
        {/* Speaker Cone Details */}
        <ellipse cx="-20" cy="0" rx="10" ry="15" fill="#111" stroke="#333" strokeWidth="1" />
        <ellipse cx="-20" cy="0" rx="4" ry="6" fill="#222" />
        <line x1="-30" y1="20" x2="-10" y2="20" stroke="#1a1a1a" strokeWidth="2" strokeLinecap="round" />
        <line x1="-30" y1="24" x2="-10" y2="24" stroke="#1a1a1a" strokeWidth="2" strokeLinecap="round" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, 10)" filter="url(#audioBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#ec4899" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 3 8 L 3 12 L 6 12 L 10 16 L 10 4 L 6 8 Z" fill="rgba(236,72,153,0.2)" />
          <path d="M 14 7 Q 16 10, 14 13" />
          <path d="M 17 4 Q 21 10, 17 16" />
        </g>
      </g>
    </g>
  </svg>
);

const AnimationStateWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="animFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="animShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="animBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      {/* Connection lines */}
      <path d="M -40 -20 L 40 -20" fill="none" stroke="#9ca3af" strokeWidth="2" />
      <polygon points="15,-25 25,-20 15,-15" fill="#9ca3af" />
      
      <path d="M 40 20 L -40 20" fill="none" stroke="#f59e0b" strokeWidth="2" />
      <polygon points="-15,15 -25,20 -15,25" fill="#f59e0b" />
      
      <g filter="url(#animShadow)">
        {/* Node 1: Idle */}
        <rect x="-70" y="-35" width="50" height="30" rx="4" fill="url(#animFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <rect x="-65" y="-20" width="40" height="4" rx="2" fill="#555" />
        
        {/* Node 2: Run (Active) */}
        <rect x="20" y="-35" width="50" height="30" rx="4" fill="url(#animFrontGrad)" stroke="#f59e0b" strokeWidth="1.5" />
        <rect x="25" y="-20" width="40" height="4" rx="2" fill="#f59e0b" />
        
        {/* Node 3: Jump */}
        <rect x="-25" y="10" width="50" height="30" rx="4" fill="url(#animFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <rect x="-20" y="25" width="40" height="4" rx="2" fill="#555" />
      </g>

      {/* Badge */}
      <g transform="translate(-15, -60)" filter="url(#animBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#f59e0b" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="10" cy="10" r="8" />
          <polygon points="8,6 14,10 8,14" fill="#f59e0b" />
        </g>
      </g>
    </g>
  </svg>
);


const PhysicsMaterialWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="physFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="physShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="physBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <radialGradient id="physBallGrad" cx="30%" cy="30%" r="70%">
        <stop offset="0%" stopColor="#fdba74" />
        <stop offset="100%" stopColor="#ea580c" />
      </radialGradient>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#physShadow)">
        {/* Ramp */}
        <path d="M -40 20 L 40 40 L 40 0 L -40 -20 Z" fill="#2d2d2d" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" strokeLinejoin="round" />
        <path d="M -40 20 L 40 40 L 40 55 L -40 35 Z" fill="url(#physFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" strokeLinejoin="round" />
        
        {/* Grid lines on ramp to indicate friction/surface */}
        <path d="M -30 10 L 30 25 M -20 0 L 20 10" fill="none" stroke="rgba(255,255,255,0.1)" strokeWidth="1" />
        
        {/* Bouncing Ball */}
        {/* Motion trail */}
        <path d="M -15 -35 Q 0 -45, 10 -25 Q 20 0, 30 15" fill="none" stroke="#f97316" strokeWidth="2" strokeDasharray="4 4" strokeLinecap="round" opacity="0.6" />
        
        <circle cx="30" cy="15" r="10" fill="url(#physBallGrad)" />
        {/* Ball squish lines */}
        <path d="M 22 25 L 38 30" fill="none" stroke="#fff" strokeWidth="1.5" strokeLinecap="round" opacity="0.4" />
        <path d="M 25 28 L 35 31" fill="none" stroke="#fff" strokeWidth="1.5" strokeLinecap="round" opacity="0.4" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, -45)" filter="url(#physBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#f97316" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="10" cy="14" r="4" fill="#f97316" opacity="0.2" />
          <path d="M 4 4 Q 10 -2, 10 10" strokeDasharray="2 2" />
          <path d="M 2 18 L 18 18" strokeWidth="1.5" />
        </g>
      </g>
    </g>
  </svg>
);

const VFXGraphWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="vfxFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="vfxShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="vfxBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <radialGradient id="vfxGlow" cx="50%" cy="50%" r="50%">
        <stop offset="0%" stopColor="#06b6d4" stopOpacity="1" />
        <stop offset="100%" stopColor="#3b82f6" stopOpacity="0" />
      </radialGradient>
    </defs>
    <g transform="translate(120, 110)">
      {/* Background Magic/VFX */}
      <circle cx="10" cy="-20" r="40" fill="url(#vfxGlow)" opacity="0.4" filter="blur(4px)" />
      
      <g filter="url(#vfxShadow)">
        {/* Graph Nodes */}
        {/* Node 1 (Input) */}
        <rect x="-50" y="-10" width="30" height="20" rx="4" fill="url(#vfxFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="-20" cy="0" r="3" fill="#06b6d4" />
        
        {/* Node 2 (Math/Logic) */}
        <rect x="-10" y="-40" width="30" height="20" rx="4" fill="url(#vfxFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="-10" cy="-30" r="3" fill="#06b6d4" />
        <circle cx="20" cy="-30" r="3" fill="#06b6d4" />
        
        {/* Node 3 (Output Particle) */}
        <rect x="30" y="-15" width="40" height="30" rx="4" fill="url(#vfxFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="30" cy="0" r="3" fill="#06b6d4" />
        
        {/* Connections */}
        <path d="M -20 0 C -10 0, -20 -30, -10 -30" fill="none" stroke="#06b6d4" strokeWidth="2" strokeLinecap="round" />
        <path d="M 20 -30 C 30 -30, 20 0, 30 0" fill="none" stroke="#06b6d4" strokeWidth="2" strokeLinecap="round" />
      </g>
      
      {/* Burst Effect */}
      <path d="M 50 -30 L 52 -40 M 55 -25 L 65 -25 M 60 -15 L 70 -5" stroke="#22d3ee" strokeWidth="2" strokeLinecap="round" />
      <circle cx="55" cy="-25" r="8" fill="url(#vfxGlow)" />

      {/* Badge */}
      <g transform="translate(-45, -50)" filter="url(#vfxBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#22d3ee" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 10 2 L 10 7 M 10 13 L 10 18 M 2 10 L 7 10 M 13 10 L 18 10 M 4.3 4.3 L 7.8 7.8 M 12.2 12.2 L 15.7 15.7 M 15.7 4.3 L 12.2 7.8 M 7.8 12.2 L 4.3 15.7" />
          <circle cx="10" cy="10" r="2" fill="#22d3ee" />
        </g>
      </g>
    </g>
  </svg>
);

const DecalWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="decalFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="decalShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="decalBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <linearGradient id="decalBeam" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#facc15" stopOpacity="0.4" />
        <stop offset="100%" stopColor="#facc15" stopOpacity="0" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 110)">
      <g filter="url(#decalShadow)">
        {/* Wall/Surface */}
        <path d="M -50 20 L 50 -10 L 50 50 L -50 80 Z" fill="url(#decalFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" strokeLinejoin="round" />
        <path d="M -50 20 L 50 -10 L 50 -15 L -50 15 Z" fill="#2d2d2d" stroke="rgba(255,255,255,0.1)" strokeWidth="1" />
        
        {/* The Decal (Splatter / Graphic) */}
        <g transform="translate(0, 30) scale(1, 0.7) rotate(15)">
          <path d="M 0 -15 C 10 -15, 15 -5, 15 0 C 15 10, 5 15, 0 15 C -10 15, -20 5, -15 -5 C -10 -10, -5 -15, 0 -15 Z" fill="#facc15" opacity="0.8" />
          <circle cx="12" cy="12" r="3" fill="#facc15" opacity="0.8" />
          <circle cx="-15" cy="8" r="4" fill="#facc15" opacity="0.8" />
          <circle cx="-5" cy="-18" r="2" fill="#facc15" opacity="0.8" />
        </g>
        
        {/* Projector Box */}
        <rect x="-15" y="-50" width="30" height="20" rx="4" fill="#111" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" />
        <polygon points="-5,-30 5,-30 10,-20 -10,-20" fill="#222" />
        
        {/* Projection Beam */}
        <polygon points="-8,-20 8,-20 25,30 -25,40" fill="url(#decalBeam)" opacity="0.6" style={{ mixBlendMode: 'screen' }} />
      </g>

      {/* Badge */}
      <g transform="translate(35, -45)" filter="url(#decalBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#facc15" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="10" cy="10" r="6" strokeDasharray="2 3" />
          <circle cx="10" cy="10" r="2" fill="#facc15" />
          <path d="M 10 1 L 10 4 M 10 16 L 10 19 M 1 10 L 4 10 M 16 10 L 19 10" strokeWidth="1.5" />
        </g>
      </g>
    </g>
  </svg>
);

const LODGroupWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="lodFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="lodShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="lodBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 130)">
      <g filter="url(#lodShadow)">
        {/* High Poly (Foreground) */}
        <g transform="translate(-30, 20)">
          <path d="M 0 -35 L 20 -5 L 0 10 L -20 -5 Z" fill="#363636" stroke="#818cf8" strokeWidth="1" />
          <path d="M 0 -35 L 0 10" stroke="#818cf8" strokeWidth="1" />
          <path d="M -20 -5 L 20 -5" stroke="#818cf8" strokeWidth="1" />
          <path d="M -10 -20 L 10 -20 L 10 3 L -10 3 Z" fill="none" stroke="#818cf8" strokeWidth="0.5" />
          <path d="M -20 -5 L 0 -20 M 20 -5 L 0 -20" stroke="#818cf8" strokeWidth="0.5" />
        </g>
        
        {/* Medium Poly (Middle) */}
        <g transform="translate(15, -5) scale(0.7)">
          <path d="M 0 -35 L 20 -5 L 0 10 L -20 -5 Z" fill="#2d2d2d" stroke="#6366f1" strokeWidth="1.5" />
          <path d="M 0 -35 L 0 10" stroke="#6366f1" strokeWidth="1.5" />
          <path d="M -20 -5 L 20 -5" stroke="#6366f1" strokeWidth="1.5" />
        </g>
        
        {/* Low Poly (Background) */}
        <g transform="translate(45, -30) scale(0.4)">
          <path d="M 0 -35 L 20 -5 L -20 -5 Z" fill="#1f1f1f" stroke="#4f46e5" strokeWidth="2.5" />
          <path d="M 0 -35 L 0 10" stroke="#4f46e5" strokeWidth="2.5" />
        </g>
        
        {/* Distance connection lines */}
        <path d="M -20 15 L 10 -15 L 40 -35" fill="none" stroke="#6366f1" strokeWidth="1.5" strokeDasharray="3 3" opacity="0.6" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, -65)" filter="url(#lodBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#818cf8" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="2" y="12" width="4" height="6" fill="#818cf8" />
          <rect x="8" y="7" width="4" height="11" fill="#818cf8" opacity="0.6" />
          <rect x="14" y="2" width="4" height="16" fill="#818cf8" opacity="0.3" />
        </g>
      </g>
    </g>
  </svg>
);

const UICanvasWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="uiFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="uiShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="uiBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      {/* 3D projection lines */}
      <path d="M -60 -40 L -40 -60 M -60 40 L -40 20 M 60 -40 L 80 -60 M 60 40 L 80 20" stroke="#a855f7" strokeWidth="1.5" strokeDasharray="4 4" opacity="0.4" />
      
      <g filter="url(#uiShadow)">
        {/* Canvas bounding box */}
        <rect x="-60" y="-40" width="120" height="80" fill="none" stroke="#a855f7" strokeWidth="1" opacity="0.5" />
        
        {/* UI Panel */}
        <g transform="translate(-10, -10)">
          <rect x="-30" y="-20" width="80" height="60" rx="4" fill="url(#uiFrontGrad)" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" />
          
          {/* Header */}
          <rect x="-30" y="-20" width="80" height="15" rx="4" fill="#a855f7" />
          <circle cx="40" cy="-12" r="3" fill="#fff" />
          <rect x="-22" y="-14" width="25" height="4" rx="2" fill="#fff" opacity="0.8" />
          
          {/* Content */}
          <rect x="-20" y="5" width="25" height="25" rx="2" fill="#2d2d2d" stroke="#555" strokeWidth="1" />
          <path d="M -15 25 L -5 10 L 0 15 L 10 5" fill="none" stroke="#d8b4fe" strokeWidth="1.5" strokeLinecap="round" />
          
          <rect x="15" y="5" width="25" height="4" rx="2" fill="#999" />
          <rect x="15" y="15" width="20" height="4" rx="2" fill="#777" />
          <rect x="15" y="25" width="15" height="4" rx="2" fill="#555" />
          
          {/* Slider */}
          <rect x="-20" y="45" width="60" height="4" rx="2" fill="#2d2d2d" />
          <rect x="-20" y="45" width="25" height="4" rx="2" fill="#a855f7" />
          <circle cx="5" cy="47" r="4" fill="#fff" />
        </g>
      </g>

      {/* Badge */}
      <g transform="translate(35, 25)" filter="url(#uiBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#a855f7" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="2" y="3" width="16" height="14" rx="2" />
          <line x1="2" y1="8" x2="18" y2="8" />
          <line x1="6" y1="5" x2="8" y2="5" />
        </g>
      </g>
    </g>
  </svg>
);


const TimelineWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="timeFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="timeShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="timeBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#timeShadow)">
        {/* Timeline Panel */}
        <rect x="-65" y="-35" width="130" height="70" rx="6" fill="url(#timeFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Top Ruler */}
        <rect x="-65" y="-35" width="130" height="15" rx="6" fill="#111" />
        <path d="M -50 -35 L -50 -25 M -30 -35 L -30 -28 M -10 -35 L -10 -25 M 10 -35 L 10 -28 M 30 -35 L 30 -25 M 50 -35 L 50 -28" stroke="#555" strokeWidth="1" />
        
        {/* Tracks */}
        <rect x="-60" y="-10" width="120" height="12" rx="2" fill="#222" />
        <rect x="-60" y="8" width="120" height="12" rx="2" fill="#222" />
        
        {/* Keyframes */}
        {/* Track 1 */}
        <polygon points="-40,-10 -36,-4 -40,2 -44,-4" fill="#3b82f6" />
        <polygon points="10,-10 14,-4 10,2 6,-4" fill="#3b82f6" />
        <rect x="-36" y="-6" width="46" height="4" fill="#3b82f6" opacity="0.3" />
        
        {/* Track 2 */}
        <polygon points="-20,8 -16,14 -20,20 -24,14" fill="#ec4899" />
        <polygon points="40,8 44,14 40,20 36,14" fill="#ec4899" />
        <rect x="-16" y="12" width="56" height="4" fill="#ec4899" opacity="0.3" />
        
        {/* Playhead */}
        <path d="M 0 -35 L 5 -25 L 1 -25 L 1 30 L -1 30 L -1 -25 L -5 -25 Z" fill="#ef4444" />
      </g>

      {/* Badge */}
      <g transform="translate(-15, 20)" filter="url(#timeBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#3b82f6" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <polygon points="7,4 15,10 7,16" fill="#3b82f6" fillOpacity="0.2" />
          <line x1="2" y1="10" x2="18" y2="10" strokeDasharray="2 2" strokeWidth="1.5" opacity="0.5" />
        </g>
      </g>
    </g>
  </svg>
);

const TilemapWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="tileFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="tileShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="tileBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#tileShadow)">
        {/* Board Base */}
        <rect x="-50" y="-50" width="100" height="100" rx="4" fill="url(#tileFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Grid */}
        <g stroke="#4b5563" strokeWidth="1" opacity="0.5">
          <line x1="-25" y1="-50" x2="-25" y2="50" />
          <line x1="0" y1="-50" x2="0" y2="50" />
          <line x1="25" y1="-50" x2="25" y2="50" />
          <line x1="-50" y1="-25" x2="50" y2="-25" />
          <line x1="-50" y1="0" x2="50" y2="0" />
          <line x1="-50" y1="25" x2="50" y2="25" />
        </g>
        
        {/* Tiles */}
        <rect x="-48" y="-48" width="21" height="21" rx="2" fill="#10b981" />
        <rect x="-23" y="-48" width="21" height="21" rx="2" fill="#10b981" />
        <rect x="2" y="-48" width="21" height="21" rx="2" fill="#3b82f6" />
        
        <rect x="-48" y="-23" width="21" height="21" rx="2" fill="#10b981" />
        <rect x="-23" y="-23" width="21" height="21" rx="2" fill="#f59e0b" />
        <rect x="2" y="-23" width="21" height="21" rx="2" fill="#3b82f6" />
        
        <rect x="-23" y="2" width="21" height="21" rx="2" fill="#f59e0b" />
        <rect x="2" y="2" width="21" height="21" rx="2" fill="#f59e0b" />
        <rect x="27" y="2" width="21" height="21" rx="2" fill="#8b5cf6" />
        
        {/* Highlighted Tile (Selection) */}
        <rect x="2" y="27" width="21" height="21" rx="2" fill="none" stroke="#fcd34d" strokeWidth="2" strokeDasharray="4 2" />
      </g>

      {/* Badge */}
      <g transform="translate(35, 35)" filter="url(#tileBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#10b981" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="4" y="4" width="12" height="12" rx="1" />
          <line x1="10" y1="4" x2="10" y2="16" />
          <line x1="4" y1="10" x2="16" y2="10" />
        </g>
      </g>
    </g>
  </svg>
);

const SpriteSheetWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="spriteFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="spriteShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="spriteBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      {/* Background Film Strip */}
      <g filter="url(#spriteShadow)">
        <rect x="-80" y="-30" width="160" height="60" rx="4" fill="url(#spriteFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Film Holes */}
        <g fill="#111">
          {[-70, -50, -30, -10, 10, 30, 50, 70].map(x => (
            <rect key={'top-'+x} x={x-3} y="-26" width="6" height="4" rx="1" />
          ))}
          {[-70, -50, -30, -10, 10, 30, 50, 70].map(x => (
            <rect key={'bot-'+x} x={x-3} y="22" width="6" height="4" rx="1" />
          ))}
        </g>
        
        {/* Frames */}
        <rect x="-65" y="-15" width="30" height="30" rx="2" fill="#2d2d2d" stroke="#555" strokeWidth="1" />
        <rect x="-25" y="-15" width="30" height="30" rx="2" fill="#2d2d2d" stroke="#555" strokeWidth="1" />
        <rect x="15" y="-15" width="30" height="30" rx="2" fill="#2d2d2d" stroke="#555" strokeWidth="1" />
        
        {/* Sprite Character Animation (Simple stick figure running) */}
        {/* Frame 1 */}
        <g transform="translate(-50, 0)" stroke="#f43f5e" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="0" cy="-6" r="3" fill="#f43f5e" />
          <path d="M 0 -3 L 0 5" />
          <path d="M 0 -1 L -6 2 M 0 -1 L 4 4" />
          <path d="M 0 5 L -4 10 M 0 5 L 4 10" />
        </g>
        {/* Frame 2 */}
        <g transform="translate(-10, 0)" stroke="#f43f5e" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="0" cy="-7" r="3" fill="#f43f5e" />
          <path d="M 0 -4 L 2 4" />
          <path d="M 1 -2 L -5 0 M 1 -2 L 5 2" />
          <path d="M 2 4 L -2 9 M 2 4 L 6 9" />
        </g>
        {/* Frame 3 */}
        <g transform="translate(30, 0)" stroke="#f43f5e" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="2" cy="-6" r="3" fill="#f43f5e" />
          <path d="M 2 -3 L 4 5" />
          <path d="M 2 -1 L -3 -2 M 3 0 L 7 1" />
          <path d="M 4 5 L 0 9 M 4 5 L 7 8" />
        </g>
        
        {/* Crop/Active Frame Highlight */}
        <rect x="-27" y="-17" width="34" height="34" rx="3" fill="none" stroke="#10b981" strokeWidth="2" strokeDasharray="4 2" />
      </g>

      {/* Badge */}
      <g transform="translate(45, -45)" filter="url(#spriteBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#f43f5e" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="3" y="3" width="14" height="14" rx="2" />
          <circle cx="10" cy="10" r="3" />
          <line x1="3" y1="7" x2="17" y2="7" />
          <line x1="3" y1="13" x2="17" y2="13" />
        </g>
      </g>
    </g>
  </svg>
);

const GizmoWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <filter id="gizmoShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="gizmoBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#gizmoShadow)">
        {/* Grid plane */}
        <path d="M -50 20 L 0 45 L 50 20 L 0 -5 Z" fill="#1f1f1f" stroke="rgba(255,255,255,0.1)" strokeWidth="1" />
        
        {/* Rotation Rings */}
        <ellipse cx="0" cy="15" rx="30" ry="12" fill="none" stroke="#34d399" strokeWidth="2" opacity="0.6" />
        <path d="M 0 -15 A 25 25 0 0 0 -25 15 A 25 25 0 0 0 0 45" fill="none" stroke="#ef4444" strokeWidth="2" opacity="0.6" transform="rotate(-30 0 15)" />
        <path d="M 0 -15 A 25 25 0 0 1 25 15 A 25 25 0 0 1 0 45" fill="none" stroke="#3b82f6" strokeWidth="2" opacity="0.6" transform="rotate(30 0 15)" />

        {/* Translation Axes */}
        {/* Y Axis (Green) */}
        <line x1="0" y1="15" x2="0" y2="-45" stroke="#34d399" strokeWidth="3" strokeLinecap="round" />
        <polygon points="-5,-40 5,-40 0,-55" fill="#34d399" />
        
        {/* X Axis (Red) */}
        <line x1="0" y1="15" x2="40" y2="35" stroke="#ef4444" strokeWidth="3" strokeLinecap="round" />
        <polygon points="35,30 40,40 50,40" fill="#ef4444" />
        
        {/* Z Axis (Blue) */}
        <line x1="0" y1="15" x2="-35" y2="30" stroke="#3b82f6" strokeWidth="3" strokeLinecap="round" />
        <polygon points="-30,25 -35,35 -45,35" fill="#3b82f6" />
        
        {/* Center Origin Point */}
        <circle cx="0" cy="15" r="5" fill="#fff" />
      </g>

      {/* Badge */}
      <g transform="translate(30, -50)" filter="url(#gizmoBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <line x1="10" y1="10" x2="10" y2="2" stroke="#34d399" />
          <line x1="10" y1="10" x2="18" y2="15" stroke="#ef4444" />
          <line x1="10" y1="10" x2="2" y2="15" stroke="#3b82f6" />
          <circle cx="10" cy="10" r="1.5" fill="#fff" stroke="none" />
        </g>
      </g>
    </g>
  </svg>
);

const LightProbeWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="probeFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="probeShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="probeBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      
      {/* Reflective Sphere Gradient */}
      <radialGradient id="probeReflection" cx="40%" cy="40%" r="60%">
        <stop offset="0%" stopColor="#fff" stopOpacity="0.9" />
        <stop offset="20%" stopColor="#e0f2fe" stopOpacity="0.7" />
        <stop offset="60%" stopColor="#38bdf8" stopOpacity="0.4" />
        <stop offset="100%" stopColor="#0f172a" stopOpacity="0.9" />
      </radialGradient>
    </defs>
    <g transform="translate(120, 120)">
      {/* Surrounding Sample Points */}
      <path d="M 0 -60 L 0 60 M -60 0 L 60 0 M -40 -40 L 40 40 M -40 40 L 40 -40" stroke="#fcd34d" strokeWidth="1" strokeDasharray="2 4" opacity="0.3" />
      <g fill="#fcd34d" opacity="0.6">
        <circle cx="0" cy="-60" r="3" />
        <circle cx="0" cy="60" r="3" />
        <circle cx="-60" cy="0" r="3" />
        <circle cx="60" cy="0" r="3" />
        <circle cx="-40" cy="-40" r="3" />
        <circle cx="40" cy="40" r="3" />
        <circle cx="-40" cy="40" r="3" />
        <circle cx="40" cy="-40" r="3" />
      </g>

      <g filter="url(#probeShadow)">
        {/* Base Stand */}
        <path d="M -10 35 L 10 35 L 15 55 L -15 55 Z" fill="#111" stroke="#333" strokeWidth="1.5" />
        <rect x="-4" y="20" width="8" height="20" fill="#2d2d2d" stroke="#555" strokeWidth="1" />
        
        {/* The Probe Sphere */}
        <circle cx="0" cy="0" r="30" fill="url(#probeReflection)" stroke="rgba(255,255,255,0.3)" strokeWidth="1.5" />
        
        {/* Reflection Highlight */}
        <ellipse cx="-8" cy="-12" rx="10" ry="6" fill="#fff" opacity="0.6" transform="rotate(-30 -8 -12)" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, -45)" filter="url(#probeBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#fcd34d" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="10" cy="10" r="5" />
          <path d="M 10 1 L 10 3 M 10 17 L 10 19 M 1 10 L 3 10 M 17 10 L 19 10" />
          <path d="M 3.5 3.5 L 5 5 M 15 15 L 16.5 16.5 M 3.5 16.5 L 5 15 M 15 5 L 16.5 3.5" />
        </g>
      </g>
    </g>
  </svg>
);

const TerrainBrushWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="brushFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="brushShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="brushBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#brushShadow)">
        {/* Terrain Surface (Curved up to show brushing) */}
        <path d="M -60 30 Q -30 30, 0 5 Q 30 -20, 60 10 L 60 50 L -60 50 Z" fill="url(#brushFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" strokeLinejoin="round" />
        
        {/* Wireframe over the terrain */}
        <path d="M -60 40 Q -30 40, 0 15 Q 30 -10, 60 20" fill="none" stroke="#10b981" strokeWidth="1" opacity="0.4" />
        <path d="M -60 50 Q -30 50, 0 25 Q 30 0, 60 30" fill="none" stroke="#10b981" strokeWidth="1" opacity="0.4" />
        <path d="M -40 30 L -40 50 M -20 20 L -20 50 M 0 5 L 0 50 M 20 -5 L 20 50 M 40 2 L 40 50" fill="none" stroke="#10b981" strokeWidth="1" opacity="0.4" />
        
        {/* Brush Circle (Projected on terrain) */}
        <ellipse cx="10" cy="0" rx="30" ry="12" fill="rgba(16,185,129,0.2)" stroke="#10b981" strokeWidth="2" strokeDasharray="4 2" />
        <ellipse cx="10" cy="0" rx="15" ry="6" fill="rgba(16,185,129,0.3)" stroke="#10b981" strokeWidth="1" />
        
        {/* Upward Arrow (Raise Terrain) */}
        <path d="M 10 0 L 10 -30 M 0 -20 L 10 -35 L 20 -20" fill="none" stroke="#fff" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, -45)" filter="url(#brushBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#10b981" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="10" cy="10" r="6" strokeDasharray="2 2" />
          <path d="M 10 12 L 10 4 M 7 7 L 10 3 L 13 7" />
        </g>
      </g>
    </g>
  </svg>
);

const AudioMixerWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="mixerFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="mixerShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="mixerBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#mixerShadow)">
        {/* Mixer Board */}
        <rect x="-60" y="-40" width="120" height="80" rx="4" fill="url(#mixerFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Channels */}
        {[-40, -15, 10, 35].map((x, i) => (
          <g key={'channel-'+i} transform={"translate(" + x + ", 0)"}>
            {/* EQ Knobs */}
            <circle cx="0" cy="-25" r="4" fill="#2d2d2d" stroke={i === 3 ? "#ef4444" : "#4b5563"} strokeWidth="1.5" />
            <circle cx="0" cy="-13" r="4" fill="#2d2d2d" stroke={i === 3 ? "#ef4444" : "#4b5563"} strokeWidth="1.5" />
            
            {/* Fader Track */}
            <rect x="-2" y="0" width="4" height="30" rx="2" fill="#111" />
            
            {/* Fader Cap */}
            <rect x="-6" y={i === 0 ? 5 : i === 1 ? 15 : i === 2 ? 0 : 20} width="12" height="6" rx="1" fill={i === 3 ? "#ef4444" : "#fff"} stroke="#999" strokeWidth="1" />
          </g>
        ))}
        
        {/* Master Level Meters */}
        <g transform="translate(65, -30)">
          {/* L */}
          <rect x="0" y="0" width="4" height="60" rx="2" fill="#111" />
          <rect x="0" y="20" width="4" height="40" rx="2" fill="#34d399" />
          <rect x="0" y="10" width="4" height="10" fill="#facc15" />
          
          {/* R */}
          <rect x="8" y="0" width="4" height="60" rx="2" fill="#111" />
          <rect x="8" y="25" width="4" height="35" rx="2" fill="#34d399" />
          <rect x="8" y="15" width="4" height="10" fill="#facc15" />
        </g>
      </g>

      {/* Badge */}
      <g transform="translate(35, -55)" filter="url(#mixerBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#ef4444" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="3" y="2" width="4" height="16" rx="2" />
          <rect x="13" y="2" width="4" height="16" rx="2" />
          <line x1="1" y1="6" x2="9" y2="6" strokeWidth="2" stroke="#ef4444" />
          <line x1="11" y1="12" x2="19" y2="12" strokeWidth="2" stroke="#ef4444" />
        </g>
      </g>
    </g>
  </svg>
);

const ProfilerWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="profFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="profShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="profBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#profShadow)">
        {/* Screen/Panel */}
        <rect x="-60" y="-45" width="120" height="90" rx="6" fill="url(#profFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Inner Screen */}
        <rect x="-50" y="-35" width="100" height="70" rx="2" fill="#111" stroke="#333" strokeWidth="1" />
        
        {/* Grid lines */}
        <path d="M -50 -15 L 50 -15 M -50 5 L 50 5 M -50 25 L 50 25" fill="none" stroke="#222" strokeWidth="1" />
        <path d="M -25 -35 L -25 35 M 0 -35 L 0 35 M 25 -35 L 25 35" fill="none" stroke="#222" strokeWidth="1" />
        
        {/* Graph 1 (CPU - Green) */}
        <path d="M -50 25 L -35 15 L -20 20 L -5 -5 L 10 10 L 25 -20 L 40 -10 L 50 -25" fill="none" stroke="#34d399" strokeWidth="2" strokeLinejoin="round" />
        <path d="M -50 35 L -50 25 L -35 15 L -20 20 L -5 -5 L 10 10 L 25 -20 L 40 -10 L 50 -25 L 50 35 Z" fill="#34d399" opacity="0.15" />
        
        {/* Graph 2 (GPU - Blue) */}
        <path d="M -50 10 L -30 20 L -10 0 L 10 -15 L 30 5 L 50 -5" fill="none" stroke="#3b82f6" strokeWidth="2" strokeLinejoin="round" opacity="0.8" />
        
        {/* Warning Spike (Red) */}
        <circle cx="25" cy="-20" r="3" fill="#ef4444" />
        <circle cx="25" cy="-20" r="6" fill="none" stroke="#ef4444" strokeWidth="1" opacity="0.6" />
      </g>

      {/* Badge */}
      <g transform="translate(35, -60)" filter="url(#profBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#34d399" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <polyline points="2,14 8,8 12,12 18,4" />
          <polyline points="14,4 18,4 18,8" />
        </g>
      </g>
    </g>
  </svg>
);

const SceneHierarchyWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="hierFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="hierShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="hierBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#hierShadow)">
        {/* Panel */}
        <rect x="-55" y="-55" width="110" height="110" rx="6" fill="url(#hierFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Header */}
        <rect x="-55" y="-55" width="110" height="20" rx="6" fill="#2d2d2d" />
        <rect x="-45" y="-48" width="30" height="6" rx="3" fill="#fff" opacity="0.8" />
        
        {/* Tree Items */}
        {/* Root */}
        <polygon points="-40,-20 -36,-16 -40,-12" fill="#fff" opacity="0.6" />
        <rect x="-30" y="-19" width="45" height="6" rx="2" fill="#fff" opacity="0.9" />
        
        {/* Child 1 */}
        <rect x="-36" y="-5" width="1" height="40" fill="#555" />
        <rect x="-36" y="-5" width="10" height="1" fill="#555" />
        <polygon points="-25,-7 -21,-3 -25,1" fill="#fff" opacity="0.6" transform="rotate(90 -23 -3)" />
        <rect x="-15" y="-6" width="40" height="6" rx="2" fill="#a855f7" /> {/* Highlighted */}
        <rect x="-50" y="-10" width="100" height="14" fill="#a855f7" opacity="0.2" /> {/* Selection bg */}
        
        {/* Grandchild */}
        <rect x="-21" y="8" width="1" height="10" fill="#555" />
        <rect x="-21" y="8" width="10" height="1" fill="#555" />
        <rect x="-5" y="7" width="30" height="6" rx="2" fill="#fff" opacity="0.7" />
        
        <rect x="-21" y="18" width="10" height="1" fill="#555" />
        <rect x="-5" y="17" width="25" height="6" rx="2" fill="#fff" opacity="0.7" />
        
        {/* Child 2 */}
        <rect x="-36" y="32" width="10" height="1" fill="#555" />
        <polygon points="-25,30 -21,34 -25,38" fill="#fff" opacity="0.6" />
        <rect x="-15" y="31" width="35" height="6" rx="2" fill="#fff" opacity="0.7" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, 30)" filter="url(#hierBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#a855f7" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="2" y="2" width="16" height="4" rx="1" />
          <rect x="6" y="9" width="12" height="4" rx="1" />
          <rect x="6" y="16" width="12" height="4" rx="1" />
          <line x1="4" y1="6" x2="4" y2="18" />
          <line x1="4" y1="11" x2="6" y2="11" />
          <line x1="4" y1="18" x2="6" y2="18" />
        </g>
      </g>
    </g>
  </svg>
);

const ColorPickerWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="colorFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="colorShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="colorBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      
      {/* Conic Gradient approximation using overlapping sweeps */}
      <linearGradient id="cgrad1" x1="0" y1="0" x2="1" y2="0"><stop offset="0%" stopColor="#f00"/><stop offset="100%" stopColor="#ff0"/></linearGradient>
      <linearGradient id="cgrad2" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stopColor="#0f0"/><stop offset="100%" stopColor="#0ff"/></linearGradient>
      <linearGradient id="cgrad3" x1="1" y1="0" x2="0" y2="0"><stop offset="0%" stopColor="#00f"/><stop offset="100%" stopColor="#f0f"/></linearGradient>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#colorShadow)">
        {/* Panel */}
        <rect x="-50" y="-55" width="100" height="110" rx="6" fill="url(#colorFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Color Wheel (Simulated) */}
        <g transform="translate(0, -15)">
          <circle cx="0" cy="0" r="28" fill="#fff" />
          <path d="M 0 -28 A 28 28 0 0 1 28 0 L 0 0 Z" fill="#ef4444" />
          <path d="M 28 0 A 28 28 0 0 1 0 28 L 0 0 Z" fill="#3b82f6" />
          <path d="M 0 28 A 28 28 0 0 1 -28 0 L 0 0 Z" fill="#10b981" />
          <path d="M -28 0 A 28 28 0 0 1 0 -28 L 0 0 Z" fill="#facc15" />
          {/* Inner gradient mask */}
          <circle cx="0" cy="0" r="28" fill="url(#colorFrontGrad)" opacity="0.3" />
          <circle cx="0" cy="0" r="14" fill="#2d2d2d" />
          
          {/* Picker Handle */}
          <circle cx="15" cy="-15" r="4" fill="none" stroke="#fff" strokeWidth="2" />
        </g>
        
        {/* Sliders */}
        <rect x="-35" y="25" width="70" height="6" rx="3" fill="#ef4444" opacity="0.8" />
        <circle cx="-10" cy="28" r="4" fill="#fff" />
        
        <rect x="-35" y="40" width="70" height="6" rx="3" fill="#fff" opacity="0.8" />
        <circle cx="20" cy="43" r="4" fill="#fff" />
      </g>

      {/* Badge */}
      <g transform="translate(30, -45)" filter="url(#colorBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#ec4899" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 14.5 2 L 18 5.5 L 6 17.5 L 2 18 L 2.5 14 Z" />
          <line x1="12" y1="4.5" x2="15.5" y2="8" />
          <path d="M 2 18 L 0 20" strokeWidth="2.5" />
        </g>
      </g>
    </g>
  </svg>
);

const TerminalWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="termFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#1a1a1a" stopOpacity="1" />
        <stop offset="100%" stopColor="#0a0a0a" stopOpacity="1" />
      </linearGradient>
      <filter id="termShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="termBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#termShadow)">
        {/* Terminal Window */}
        <rect x="-60" y="-45" width="120" height="90" rx="6" fill="url(#termFrontGrad)" stroke="#333" strokeWidth="1.5" />
        
        {/* Header Bar */}
        <path d="M -60 -39 C -60 -42.3, -57.3 -45, -54 -45 L 54 -45 C 57.3 -45, 60 -42.3, 60 -39 L 60 -25 L -60 -25 Z" fill="#2d2d2d" />
        <circle cx="-50" cy="-35" r="3" fill="#ef4444" />
        <circle cx="-40" cy="-35" r="3" fill="#facc15" />
        <circle cx="-30" cy="-35" r="3" fill="#22c55e" />
        
        {/* Code/Text Lines */}
        <g transform="translate(-50, -10)">
          <path d="M 0 0 L 4 4 L 0 8" fill="none" stroke="#22c55e" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
          <rect x="8" y="2" width="40" height="4" rx="2" fill="#fff" opacity="0.8" />
          
          <rect x="8" y="14" width="70" height="4" rx="2" fill="#9ca3af" />
          <rect x="8" y="24" width="55" height="4" rx="2" fill="#9ca3af" />
          
          <path d="M 0 36 L 4 40 L 0 44" fill="none" stroke="#22c55e" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
          <rect x="8" y="38" width="6" height="4" fill="#22c55e" opacity="0.8" />
        </g>
      </g>

      {/* Badge */}
      <g transform="translate(35, 25)" filter="url(#termBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#22c55e" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <polyline points="4,4 10,10 4,16" />
          <line x1="12" y1="16" x2="18" y2="16" />
        </g>
      </g>
    </g>
  </svg>
);

const BuildWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="buildFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="buildShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="buildBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#buildShadow)">
        {/* Gear 1 */}
        <g transform="translate(-15, -10) scale(1.2)">
          <path d="M 0 -20 L 4 -20 L 6 -15 L 12 -12 L 17 -16 L 20 -13 L 15 -8 L 17 -2 L 22 0 L 22 4 L 17 6 L 15 12 L 20 16 L 17 20 L 12 15 L 6 17 L 4 22 L 0 22 L -2 17 L -8 15 L -13 20 L -16 17 L -12 12 L -14 6 L -19 4 L -19 0 L -14 -2 L -12 -8 L -16 -13 L -13 -16 L -8 -12 L -2 -15 Z" fill="#2d2d2d" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" strokeLinejoin="round" />
          <circle cx="2" cy="1" r="6" fill="#111" />
        </g>
        
        {/* Gear 2 (Smaller, intersecting) */}
        <g transform="translate(15, 20) rotate(15)">
          <path d="M 0 -15 L 3 -15 L 4 -11 L 9 -9 L 13 -12 L 15 -10 L 11 -6 L 13 -2 L 17 0 L 17 3 L 13 4 L 11 9 L 15 13 L 13 15 L 9 11 L 4 13 L 3 17 L 0 17 L -2 13 L -6 11 L -10 15 L -12 13 L -9 9 L -10 4 L -14 3 L -14 0 L -10 -2 L -9 -6 L -12 -10 L -10 -12 L -6 -9 L -2 -11 Z" fill="#3b82f6" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" strokeLinejoin="round" />
          <circle cx="1" cy="1" r="4" fill="#111" />
        </g>
        
        {/* Hammer */}
        <g transform="translate(15, -15) rotate(45)">
          <rect x="-4" y="-2" width="8" height="35" rx="2" fill="#854d0e" stroke="rgba(255,255,255,0.15)" strokeWidth="1" />
          <rect x="-12" y="-12" width="24" height="12" rx="2" fill="#9ca3af" stroke="rgba(255,255,255,0.3)" strokeWidth="1.5" />
          <path d="M 12 -9 L 16 -6 L 12 -3 Z" fill="#9ca3af" />
        </g>
      </g>

      {/* Badge */}
      <g transform="translate(-45, 20)" filter="url(#buildBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#3b82f6" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <polygon points="10,2 18,6 18,14 10,18 2,14 2,6" />
          <circle cx="10" cy="10" r="3" />
        </g>
      </g>
    </g>
  </svg>
);

const XRWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="xrFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="xrShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="xrBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#xrShadow)">
        {/* Headband */}
        <path d="M -50 -10 Q 0 -30, 50 -10 L 50 10 Q 0 -10, -50 10 Z" fill="#2d2d2d" stroke="rgba(255,255,255,0.1)" strokeWidth="1.5" />
        
        {/* Visor */}
        <rect x="-40" y="-5" width="80" height="35" rx="12" fill="url(#xrFrontGrad)" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" />
        
        {/* Front Glass */}
        <rect x="-35" y="0" width="70" height="25" rx="8" fill="#111" />
        
        {/* Lenses / Cameras */}
        <circle cx="-15" cy="12" r="5" fill="#2d2d2d" stroke="#8b5cf6" strokeWidth="1.5" />
        <circle cx="15" cy="12" r="5" fill="#2d2d2d" stroke="#8b5cf6" strokeWidth="1.5" />
        
        <circle cx="-15" cy="12" r="2" fill="#8b5cf6" />
        <circle cx="15" cy="12" r="2" fill="#8b5cf6" />
        
        {/* Glowing Trim */}
        <path d="M -35 12 Q 0 25, 35 12" fill="none" stroke="#8b5cf6" strokeWidth="2" strokeLinecap="round" opacity="0.6" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, -45)" filter="url(#xrBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#8b5cf6" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 3 6 L 17 6 C 18.5 6, 19 7, 19 9 L 19 13 C 19 15, 18 16, 16 16 L 13 16 L 11 12 L 9 12 L 7 16 L 4 16 C 2 16, 1 15, 1 13 L 1 9 C 1 7, 1.5 6, 3 6 Z" />
        </g>
      </g>
    </g>
  </svg>
);

const CameraFrustumWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="camFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="camShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="camBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <linearGradient id="camFrustum" x1="0" y1="0" x2="1" y2="0">
        <stop offset="0%" stopColor="#38bdf8" stopOpacity="0.4" />
        <stop offset="100%" stopColor="#38bdf8" stopOpacity="0" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 120)">
      {/* Frustum Area */}
      <polygon points="10,-10 10,10 60,40 60,-40" fill="url(#camFrustum)" style={{ mixBlendMode: 'screen' }} />
      <path d="M 10 -10 L 60 -40 M 10 10 L 60 40 M 10 -10 L 10 10 M 60 -40 L 60 40" fill="none" stroke="#38bdf8" strokeWidth="1" strokeDasharray="4 2" opacity="0.6" />
      <rect x="50" y="-30" width="10" height="60" fill="none" stroke="#38bdf8" strokeWidth="1" strokeDasharray="2 2" opacity="0.4" />
      
      <g filter="url(#camShadow)">
        {/* Camera Body */}
        <rect x="-40" y="-20" width="40" height="30" rx="4" fill="url(#camFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <path d="M -40 -20 L 0 -20 L 0 -10 L -40 -10 Z" fill="#2d2d2d" />
        
        {/* Film Reels */}
        <circle cx="-25" cy="-25" r="12" fill="#111" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="-25" cy="-25" r="4" fill="#333" />
        <circle cx="5" cy="-25" r="12" fill="#111" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <circle cx="5" cy="-25" r="4" fill="#333" />
        
        {/* Lens */}
        <path d="M 0 -10 L 15 -15 L 15 15 L 0 10 Z" fill="#222" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" strokeLinejoin="round" />
        <ellipse cx="15" cy="0" rx="4" ry="15" fill="#38bdf8" opacity="0.8" />
        <ellipse cx="13" cy="0" rx="2" ry="12" fill="#fff" opacity="0.5" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, 25)" filter="url(#camBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#38bdf8" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="2" y="5" width="10" height="10" rx="2" />
          <polygon points="12,8 18,5 18,15 12,12" />
        </g>
      </g>
    </g>
  </svg>
);

const BehaviorTreeWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="btFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="btShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="btBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      {/* Connections */}
      <path d="M 0 -35 L 0 -15 M 0 -15 L -35 5 M 0 -15 L 35 5" fill="none" stroke="#9ca3af" strokeWidth="2" />
      
      <g filter="url(#btShadow)">
        {/* Root Node (Selector / ? ) */}
        <rect x="-20" y="-55" width="40" height="20" rx="4" fill="url(#btFrontGrad)" stroke="#3b82f6" strokeWidth="1.5" />
        <text x="0" y="-41" fill="#3b82f6" fontSize="12" fontWeight="bold" textAnchor="middle" fontFamily="sans-serif">?</text>
        
        {/* Sequence Node ( -> ) */}
        <rect x="-55" y="5" width="40" height="20" rx="4" fill="url(#btFrontGrad)" stroke="#10b981" strokeWidth="1.5" />
        <text x="-35" y="19" fill="#10b981" fontSize="12" fontWeight="bold" textAnchor="middle" fontFamily="sans-serif">→</text>
        
        {/* Action Node ( Leaf ) */}
        <rect x="15" y="5" width="40" height="20" rx="4" fill="url(#btFrontGrad)" stroke="#f43f5e" strokeWidth="1.5" />
        <rect x="25" y="13" width="20" height="4" rx="1" fill="#f43f5e" />
        
        {/* Active Highlight on Action */}
        <rect x="13" y="3" width="44" height="24" rx="6" fill="none" stroke="#f43f5e" strokeWidth="2" strokeDasharray="4 2" opacity="0.6" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, -55)" filter="url(#btBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#10b981" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="7" y="2" width="6" height="4" />
          <rect x="2" y="12" width="6" height="4" />
          <rect x="12" y="12" width="6" height="4" />
          <path d="M 10 6 L 10 9 M 10 9 L 5 12 M 10 9 L 15 12" />
        </g>
      </g>
    </g>
  </svg>
);

const UVEditorWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="uvFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="uvShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="uvBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      <pattern id="checker" width="16" height="16" patternUnits="userSpaceOnUse">
        <rect width="8" height="8" fill="#1f1f1f" />
        <rect x="8" width="8" height="8" fill="#2d2d2d" />
        <rect y="8" width="8" height="8" fill="#2d2d2d" />
        <rect x="8" y="8" width="8" height="8" fill="#1f1f1f" />
      </pattern>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#uvShadow)">
        {/* Editor Window */}
        <rect x="-55" y="-55" width="110" height="110" rx="6" fill="url(#uvFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <rect x="-45" y="-45" width="90" height="90" fill="url(#checker)" stroke="#444" strokeWidth="1" />
        
        {/* UV Shell */}
        <g stroke="#a855f7" strokeWidth="1.5" fill="rgba(168, 85, 247, 0.2)" strokeLinejoin="round">
          <polygon points="-25,-25 15,-25 25,15 -15,25" />
          <polygon points="15,-25 35,-5 25,15" />
          <polygon points="-25,-25 -35,-5 -15,25" />
        </g>
        
        {/* Vertices */}
        <circle cx="-25" cy="-25" r="2.5" fill="#a855f7" />
        <circle cx="15" cy="-25" r="2.5" fill="#a855f7" />
        <circle cx="25" cy="15" r="2.5" fill="#a855f7" />
        <circle cx="-15" cy="25" r="2.5" fill="#a855f7" />
        <circle cx="35" cy="-5" r="2.5" fill="#a855f7" />
        <circle cx="-35" cy="-5" r="2.5" fill="#a855f7" />
        
        {/* Selected Vertex/Edge */}
        <line x1="-15" y1="25" x2="25" y2="15" stroke="#fcd34d" strokeWidth="2" />
        <circle cx="5" cy="20" r="3" fill="#fcd34d" />
      </g>

      {/* Badge */}
      <g transform="translate(30, 30)" filter="url(#uvBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#a855f7" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="3" y="3" width="14" height="14" rx="2" />
          <path d="M 3 10 L 17 10 M 10 3 L 10 17" opacity="0.5" />
          <circle cx="10" cy="10" r="2" fill="#a855f7" />
        </g>
      </g>
    </g>
  </svg>
);

const GamepadInputWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="padFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="padShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="padBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#padShadow)">
        {/* Gamepad Body */}
        <path d="M -50 -10 C -50 -30, -30 -30, -15 -20 C 0 -25, 15 -20, 30 -30 C 50 -30, 50 -10, 50 10 C 50 40, 30 40, 20 20 C 10 30, -10 30, -20 20 C -30 40, -50 40, -50 10 Z" fill="url(#padFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* D-Pad */}
        <path d="M -35 -5 L -25 -5 L -25 -15 L -15 -15 L -15 -5 L -5 -5 L -5 5 L -15 5 L -15 15 L -25 15 L -25 5 L -35 5 Z" fill="#2d2d2d" stroke="#111" strokeWidth="1" />
        
        {/* Action Buttons */}
        <circle cx="25" cy="-15" r="4" fill="#3b82f6" /> {/* X */}
        <circle cx="35" cy="-5" r="4" fill="#ef4444" /> {/* B */}
        <circle cx="15" cy="-5" r="4" fill="#8b5cf6" /> {/* Y */}
        <circle cx="25" cy="5" r="4" fill="#10b981" /> {/* A */}
        
        {/* Analog Sticks */}
        <circle cx="-15" cy="15" r="10" fill="#111" />
        <circle cx="-15" cy="15" r="7" fill="#2d2d2d" />
        
        <circle cx="5" cy="15" r="10" fill="#111" />
        <circle cx="5" cy="15" r="7" fill="#2d2d2d" />
        
        {/* Center Buttons */}
        <rect x="-5" y="-10" width="10" height="4" rx="2" fill="#555" />
      </g>

      {/* Input Mapping Lines */}
      <path d="M 25 5 L 45 35" fill="none" stroke="#10b981" strokeWidth="1.5" strokeDasharray="3 3" />
      <rect x="40" y="35" width="25" height="12" rx="2" fill="#10b981" />
      <text x="52" y="44" fill="#fff" fontSize="8" fontWeight="bold" textAnchor="middle" fontFamily="sans-serif">JUMP</text>

      {/* Badge */}
      <g transform="translate(-45, -50)" filter="url(#padBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#f43f5e" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="2" y="5" width="16" height="10" rx="4" />
          <circle cx="6" cy="10" r="1.5" fill="#f43f5e" />
          <circle cx="14" cy="10" r="1.5" fill="#f43f5e" />
        </g>
      </g>
    </g>
  </svg>
);

const PostProcessingWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="postFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="postShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="postBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
      {/* Bloom Effect */}
      <filter id="bloom">
        <feGaussianBlur stdDeviation="4" result="blur" />
        <feMerge>
          <feMergeNode in="blur" />
          <feMergeNode in="SourceGraphic" />
        </feMerge>
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#postShadow)">
        {/* Screen/Viewport */}
        <rect x="-60" y="-40" width="120" height="80" rx="4" fill="url(#postFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Left Side (Raw) */}
        <g clipPath="url(#leftClip)">
          <clipPath id="leftClip"><rect x="-60" y="-40" width="60" height="80" /></clipPath>
          <circle cx="-15" cy="-5" r="15" fill="#a1a1aa" />
          <polygon points="-40,25 -20,-5 0,25" fill="#71717a" />
        </g>
        
        {/* Right Side (Post-Processed: Bloom + Color Grade) */}
        <g clipPath="url(#rightClip)">
          <clipPath id="rightClip"><rect x="0" y="-40" width="60" height="80" /></clipPath>
          <rect x="0" y="-40" width="60" height="80" fill="#0f172a" opacity="0.5" /> {/* Color tint */}
          <circle cx="15" cy="-5" r="15" fill="#38bdf8" filter="url(#bloom)" />
          <polygon points="0,25 20,-5 40,25" fill="#818cf8" filter="url(#bloom)" />
        </g>
        
        {/* Split Line */}
        <line x1="0" y1="-40" x2="0" y2="40" stroke="#fff" strokeWidth="2" />
        <circle cx="0" cy="0" r="4" fill="#fff" />
        <path d="M -3 -3 L -6 0 L -3 3 M 3 -3 L 6 0 L 3 3" fill="none" stroke="#111" strokeWidth="1" />
      </g>

      {/* Badge */}
      <g transform="translate(35, 20)" filter="url(#postBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#38bdf8" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 4 16 L 16 4" />
          <circle cx="6" cy="6" r="2" fill="#38bdf8" />
          <circle cx="14" cy="14" r="2" />
        </g>
      </g>
    </g>
  </svg>
);

const SplinePathWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="splineFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="splineShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="splineBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#splineShadow)">
        {/* Background Grid */}
        <rect x="-60" y="-45" width="120" height="90" rx="4" fill="url(#splineFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g stroke="#333" strokeWidth="1" opacity="0.5">
          <line x1="-40" y1="-45" x2="-40" y2="45" />
          <line x1="-20" y1="-45" x2="-20" y2="45" />
          <line x1="0" y1="-45" x2="0" y2="45" />
          <line x1="20" y1="-45" x2="20" y2="45" />
          <line x1="40" y1="-45" x2="40" y2="45" />
          <line x1="-60" y1="-25" x2="60" y2="-25" />
          <line x1="-60" y1="-5" x2="60" y2="-5" />
          <line x1="-60" y1="15" x2="60" y2="15" />
          <line x1="-60" y1="35" x2="60" y2="35" />
        </g>
        
        {/* Tangent Handles */}
        <line x1="-30" y1="10" x2="-50" y2="-20" stroke="#fcd34d" strokeWidth="1.5" opacity="0.6" />
        <line x1="0" y1="-20" x2="-20" y2="15" stroke="#fcd34d" strokeWidth="1.5" opacity="0.6" />
        <line x1="0" y1="-20" x2="20" y2="-55" stroke="#fcd34d" strokeWidth="1.5" opacity="0.6" />
        <line x1="40" y1="20" x2="25" y2="5" stroke="#fcd34d" strokeWidth="1.5" opacity="0.6" />
        
        {/* Tangent Points */}
        <circle cx="-50" cy="-20" r="2.5" fill="#fcd34d" />
        <circle cx="-20" cy="15" r="2.5" fill="#fcd34d" />
        <circle cx="20" cy="-55" r="2.5" fill="#fcd34d" />
        <circle cx="25" cy="5" r="2.5" fill="#fcd34d" />
        
        {/* The Spline Curve */}
        <path d="M -30 10 C -50 -20, -20 15, 0 -20 S 25 5, 40 20" fill="none" stroke="#22c55e" strokeWidth="3" />
        
        {/* Control Points */}
        <circle cx="-30" cy="10" r="4" fill="#fff" stroke="#22c55e" strokeWidth="2" />
        <circle cx="0" cy="-20" r="4" fill="#fff" stroke="#22c55e" strokeWidth="2" />
        <circle cx="40" cy="20" r="4" fill="#fff" stroke="#22c55e" strokeWidth="2" />
      </g>

      {/* Badge */}
      <g transform="translate(30, -60)" filter="url(#splineBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#22c55e" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 4 16 C 4 8, 16 12, 16 4" />
          <circle cx="4" cy="16" r="1.5" fill="#22c55e" />
          <circle cx="16" cy="4" r="1.5" fill="#22c55e" />
        </g>
      </g>
    </g>
  </svg>
);

const ParticleEmitterWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="partFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="partShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="partBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#partShadow)">
        {/* Emitter Base */}
        <polygon points="-15,40 15,40 10,20 -10,20" fill="url(#partFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <rect x="-10" y="15" width="20" height="5" rx="2" fill="#3b82f6" />
        
        {/* Particles */}
        {/* Sparkles / Orbs */}
        <circle cx="0" cy="5" r="4" fill="#fcd34d" opacity="0.9" />
        <circle cx="-15" cy="-5" r="3" fill="#f43f5e" opacity="0.8" />
        <circle cx="20" cy="-10" r="5" fill="#38bdf8" opacity="0.7" />
        <circle cx="-25" cy="-25" r="2" fill="#a855f7" opacity="0.9" />
        <circle cx="10" cy="-35" r="3" fill="#10b981" opacity="0.6" />
        <circle cx="-5" cy="-50" r="4" fill="#fcd34d" opacity="0.8" />
        <circle cx="30" cy="-45" r="2.5" fill="#f43f5e" opacity="0.9" />
        
        {/* Motion trails */}
        <path d="M 0 15 L 0 9 M -8 15 L -13 0 M 8 15 L 15 -2 M -2 15 L -20 -15 M 2 15 L 8 -25 M 5 15 L 25 -35" fill="none" stroke="#fff" strokeWidth="1" strokeDasharray="2 2" opacity="0.3" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, 25)" filter="url(#partBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#fcd34d" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="10" cy="10" r="2" />
          <path d="M 10 2 L 10 5 M 10 15 L 10 18 M 2 10 L 5 10 M 15 10 L 18 10 M 4.5 4.5 L 6.5 6.5 M 13.5 13.5 L 15.5 15.5 M 4.5 15.5 L 6.5 13.5 M 13.5 4.5 L 15.5 6.5" />
        </g>
      </g>
    </g>
  </svg>
);

const PhysicsRigidbodyWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="physFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="physShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="physBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#physShadow)">
        {/* Slope/Ground */}
        <polygon points="-60,10 -20,10 40,40 -60,40" fill="url(#physFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Falling/Bouncing Cube */}
        <g transform="translate(0, 0) rotate(15)">
          <rect x="-15" y="-15" width="30" height="30" rx="3" fill="#f43f5e" stroke="rgba(255,255,255,0.2)" strokeWidth="1.5" />
          <path d="M -15 -15 L 0 0 M 15 -15 L 0 0 M 15 15 L 0 0 M -15 15 L 0 0" stroke="rgba(0,0,0,0.2)" strokeWidth="1.5" />
        </g>
        
        {/* Gravity Vector */}
        <path d="M 0 0 L 0 25" fill="none" stroke="#10b981" strokeWidth="2" strokeDasharray="4 2" />
        <polygon points="-3,20 3,20 0,27" fill="#10b981" />
        
        {/* Velocity/Bounce Vector */}
        <path d="M 0 0 L 25 -15" fill="none" stroke="#38bdf8" strokeWidth="2" />
        <polygon points="18,-14 22,-8 28,-17" fill="#38bdf8" />
        
        {/* Rotation indicator */}
        <path d="M -25 -15 A 20 20 0 0 1 -5 -35" fill="none" stroke="#fcd34d" strokeWidth="2" strokeDasharray="3 3" />
        <polygon points="-10,-37 -2,-34 -7,-28" fill="#fcd34d" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, -50)" filter="url(#physBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#f43f5e" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <polygon points="10,2 18,7 18,17 10,22 2,17 2,7" />
          <path d="M 2 7 L 10 12 L 18 7 M 10 12 L 10 22" />
        </g>
      </g>
    </g>
  </svg>
);

const AnimationCurveWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="curveFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="curveShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="curveBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#curveShadow)">
        {/* Graph Area */}
        <rect x="-60" y="-45" width="120" height="90" rx="4" fill="url(#curveFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Grid lines */}
        <g stroke="#333" strokeWidth="1">
          <line x1="-60" y1="-15" x2="60" y2="-15" />
          <line x1="-60" y1="15" x2="60" y2="15" />
          <line x1="-20" y1="-45" x2="-20" y2="45" />
          <line x1="20" y1="-45" x2="20" y2="45" />
        </g>
        
        {/* The Curve */}
        <path d="M -50 25 C -20 25, -20 -25, 10 -25 S 40 10, 50 10" fill="none" stroke="#a855f7" strokeWidth="2.5" />
        
        {/* Keyframes */}
        <polygon points="-50,22 -47,25 -50,28 -53,25" fill="#fcd34d" stroke="#111" strokeWidth="1" />
        <polygon points="10,-28 13,-25 10,-22 7,-25" fill="#fcd34d" stroke="#111" strokeWidth="1" />
        <polygon points="50,7 53,10 50,13 47,10" fill="#fcd34d" stroke="#111" strokeWidth="1" />
        
        {/* Tangent Handles on Middle Keyframe */}
        <line x1="-10" y1="-25" x2="30" y2="-25" stroke="#fff" strokeWidth="1" opacity="0.6" />
        <circle cx="-10" cy="-25" r="2" fill="#fff" />
        <circle cx="30" cy="-25" r="2" fill="#fff" />
      </g>

      {/* Badge */}
      <g transform="translate(30, 25)" filter="url(#curveBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#a855f7" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 2 16 C 8 16, 8 4, 14 4 S 18 12, 20 12" />
          <circle cx="2" cy="16" r="1" />
          <circle cx="14" cy="4" r="1" />
          <circle cx="20" cy="12" r="1" />
        </g>
      </g>
    </g>
  </svg>
);



const ShaderNodeWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="nodeFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="nodeShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="nodeBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      {/* Background connections */}
      <path d="M -60 -20 C -40 -20, -40 -10, -20 -10" fill="none" stroke="#f43f5e" strokeWidth="2" />
      <path d="M -60 10 C -40 10, -40 5, -20 5" fill="none" stroke="#a855f7" strokeWidth="2" />
      <path d="M 20 -5 C 40 -5, 40 -25, 60 -25" fill="none" stroke="#10b981" strokeWidth="2" />

      <g filter="url(#nodeShadow)">
        {/* Node Window */}
        <rect x="-20" y="-35" width="40" height="60" rx="4" fill="url(#nodeFrontGrad)" stroke="#333" strokeWidth="1.5" />
        
        {/* Header */}
        <path d="M -20 -31 C -20 -33.2, -18.2 -35, -16 -35 L 16 -35 C 18.2 -35, 20 -33.2, 20 -31 L 20 -20 L -20 -20 Z" fill="#3b82f6" />
        <rect x="-12" y="-29" width="24" height="4" rx="2" fill="#fff" opacity="0.8" />
        
        {/* Input Pins */}
        <circle cx="-20" cy="-10" r="3" fill="#f43f5e" />
        <circle cx="-20" cy="5" r="3" fill="#a855f7" />
        <circle cx="-20" cy="15" r="3" fill="#fcd34d" />
        
        {/* Output Pins */}
        <circle cx="20" cy="-5" r="3" fill="#10b981" />
        
        {/* Inner lines (visual design) */}
        <rect x="-12" y="-12" width="10" height="3" rx="1.5" fill="#fff" opacity="0.4" />
        <rect x="-12" y="3" width="14" height="3" rx="1.5" fill="#fff" opacity="0.4" />
        <rect x="2" y="-6" width="10" height="3" rx="1.5" fill="#fff" opacity="0.4" />
      </g>

      {/* Badge */}
      <g transform="translate(30, 20)" filter="url(#nodeBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#3b82f6" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <rect x="4" y="4" width="12" height="12" rx="2" />
          <line x1="2" y1="8" x2="4" y2="8" />
          <line x1="2" y1="12" x2="4" y2="12" />
          <line x1="16" y1="10" x2="18" y2="10" />
        </g>
      </g>
    </g>
  </svg>
);

const NavMeshWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="navFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="navShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="navBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#navShadow)">
        {/* Isometric base */}
        <polygon points="0,-40 60,-10 0,20 -60,-10" fill="url(#navFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <polygon points="-60,-10 0,20 0,35 -60,5" fill="#222" />
        <polygon points="60,-10 0,20 0,35 60,5" fill="#111" />
        
        {/* NavMesh Polygons (Blue tint over the surface) */}
        <g fill="rgba(56, 189, 248, 0.2)" stroke="#38bdf8" strokeWidth="1" strokeLinejoin="round">
          <polygon points="0,-35 40,-15 10,-5 -20,-25" />
          <polygon points="10,-5 40,-15 50,-5 20,10" />
          <polygon points="-20,-25 10,-5 -10,10 -40,-10" />
          <polygon points="-10,10 10,-5 20,10 0,25" />
        </g>
        
        {/* Hole/Obstacle (no navmesh) */}
        <polygon points="0,25 20,10 50,20 30,35" fill="none" stroke="#ef4444" strokeWidth="1" strokeDasharray="2 2" opacity="0.5" />
        
        {/* Path line */}
        <path d="M -30 -15 L -5 0 L 25 -5 L 45 0" fill="none" stroke="#fcd34d" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" />
        
        {/* Start/End markers */}
        <circle cx="-30" cy="-15" r="3" fill="#10b981" />
        <circle cx="45" cy="0" r="3" fill="#f43f5e" />
      </g>

      {/* Badge */}
      <g transform="translate(25, -60)" filter="url(#navBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#38bdf8" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <polygon points="10,2 18,6 10,10 2,6" />
          <polygon points="10,10 18,14 10,18 2,14" />
          <path d="M 2 10 L 10 14 L 18 10" />
        </g>
      </g>
    </g>
  </svg>
);

const RaycastWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="rayFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="rayShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="rayBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#rayShadow)">
        {/* Origin Emitter */}
        <circle cx="-40" cy="-40" r="8" fill="url(#rayFrontGrad)" stroke="#10b981" strokeWidth="2" />
        <circle cx="-40" cy="-40" r="3" fill="#10b981" />
        
        {/* Target Object (A hexagon or angled wall) */}
        <polygon points="20,10 50,-10 60,20 30,40" fill="url(#rayFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        
        {/* Ray */}
        <line x1="-35" y1="-35" x2="28" y2="15" stroke="#10b981" strokeWidth="2" strokeDasharray="6 3" />
        
        {/* Hit Point */}
        <circle cx="28" cy="15" r="4" fill="#ef4444" />
        <circle cx="28" cy="15" r="8" fill="none" stroke="#ef4444" strokeWidth="1" opacity="0.6" />
        
        {/* Hit Normal */}
        <line x1="28" y1="15" x2="10" y2="35" stroke="#38bdf8" strokeWidth="2" />
        <polygon points="9,30 6,39 15,35" fill="#38bdf8" />
      </g>

      {/* Badge */}
      <g transform="translate(-50, 15)" filter="url(#rayBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#10b981" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <circle cx="4" cy="4" r="2" />
          <line x1="6" y1="6" x2="16" y2="16" />
          <polyline points="12,16 16,16 16,12" />
        </g>
      </g>
    </g>
  </svg>
);

const AudioSourceWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="audioFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="audioShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="audioBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#audioShadow)">
        {/* Speaker Cone */}
        <path d="M -20 -15 L -20 15 L -5 15 L 15 35 L 15 -35 L -5 -15 Z" fill="url(#audioFrontGrad)" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" strokeLinejoin="round" />
        <line x1="-5" y1="-15" x2="-5" y2="15" stroke="#1f1f1f" strokeWidth="1.5" />
        
        {/* Sound Waves */}
        <path d="M 25 -10 A 15 15 0 0 1 25 10" fill="none" stroke="#38bdf8" strokeWidth="2.5" strokeLinecap="round" />
        <path d="M 35 -20 A 30 30 0 0 1 35 20" fill="none" stroke="#3b82f6" strokeWidth="2.5" strokeLinecap="round" opacity="0.7" />
        <path d="M 45 -30 A 45 45 0 0 1 45 30" fill="none" stroke="#818cf8" strokeWidth="2.5" strokeLinecap="round" opacity="0.4" />
      </g>

      {/* Badge */}
      <g transform="translate(-45, 15)" filter="url(#audioBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#38bdf8" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M 4 8 L 4 12 L 8 12 L 14 16 L 14 4 L 8 8 Z" />
          <path d="M 16 7 A 4 4 0 0 1 16 13" />
        </g>
      </g>
    </g>
  </svg>
);

const IKRigWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="ikFrontGrad" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#363636" stopOpacity="1" />
        <stop offset="100%" stopColor="#1f1f1f" stopOpacity="1" />
      </linearGradient>
      <filter id="ikShadow">
        <feDropShadow dx="0" dy="12" stdDeviation="15" floodColor="#000" floodOpacity="0.4" />
      </filter>
      <filter id="ikBadgeShadow">
        <feDropShadow dx="0" dy="6" stdDeviation="8" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <g filter="url(#ikShadow)">
        {/* Bones (Polygons) */}
        <polygon points="-30,-30 -15,0 -20,5 -40,-25" fill="url(#ikFrontGrad)" stroke="#a855f7" strokeWidth="1.5" strokeLinejoin="round" />
        <polygon points="-15,0 25,10 20,15 -20,5" fill="url(#ikFrontGrad)" stroke="#a855f7" strokeWidth="1.5" strokeLinejoin="round" />
        <polygon points="25,10 45,-5 40,-10 20,15" fill="url(#ikFrontGrad)" stroke="#a855f7" strokeWidth="1.5" strokeLinejoin="round" />
        
        {/* Joints */}
        <circle cx="-30" cy="-30" r="5" fill="#fcd34d" stroke="#333" strokeWidth="1" />
        <circle cx="-15" cy="0" r="5" fill="#fcd34d" stroke="#333" strokeWidth="1" />
        <circle cx="25" cy="10" r="5" fill="#fcd34d" stroke="#333" strokeWidth="1" />
        
        {/* IK Target Handle */}
        <circle cx="45" cy="-5" r="6" fill="#ef4444" stroke="#fff" strokeWidth="1.5" />
        <path d="M 45 -15 L 45 -25 M 45 5 L 45 15 M 35 -5 L 25 -5 M 55 -5 L 65 -5" fill="none" stroke="#ef4444" strokeWidth="2" strokeLinecap="round" opacity="0.8" />
        
        {/* Pole Vector Target */}
        <line x1="-15" y1="0" x2="-25" y2="35" stroke="#10b981" strokeWidth="2" strokeDasharray="3 3" />
        <circle cx="-25" cy="35" r="4" fill="#10b981" />
      </g>

      {/* Badge */}
      <g transform="translate(-50, 10)" filter="url(#ikBadgeShadow)">
        <rect width="40" height="40" rx="12" fill="#262626" stroke="rgba(255,255,255,0.15)" strokeWidth="1.5" />
        <g transform="translate(10, 10)" fill="none" stroke="#a855f7" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <line x1="4" y1="4" x2="10" y2="16" />
          <line x1="10" y1="16" x2="16" y2="8" />
          <circle cx="4" cy="4" r="1.5" />
          <circle cx="10" cy="16" r="1.5" />
          <circle cx="16" cy="8" r="1.5" />
        </g>
      </g>
    </g>
  </svg>
);

const FluidSimWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="fluidGrad" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#0ea5e9" stopOpacity="0.8" />
        <stop offset="50%" stopColor="#3b82f6" stopOpacity="0.4" />
        <stop offset="100%" stopColor="#1e3a8a" stopOpacity="0.9" />
      </linearGradient>
      <filter id="fluidGlow" x="-20%" y="-20%" width="140%" height="140%">
        <feGaussianBlur stdDeviation="8" result="blur" />
        <feComposite in="SourceGraphic" in2="blur" operator="over" />
      </filter>
      <linearGradient id="fluidBg" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#0f172a" stopOpacity="1" />
        <stop offset="100%" stopColor="#020617" stopOpacity="1" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 120)">
      <rect x="-80" y="-80" width="160" height="160" rx="16" fill="url(#fluidBg)" stroke="rgba(255,255,255,0.1)" strokeWidth="1" />
      
      {/* Density Clouds */}
      <g filter="url(#fluidGlow)">
        <path d="M -60 -40 C -20 -60, 20 -20, 60 -40 C 70 0, 40 40, 20 60 C -20 50, -60 20, -60 -40 Z" fill="url(#fluidGrad)" style={{ mixBlendMode: 'screen' }} />
        <path d="M -40 20 C -10 60, 40 40, 50 10 C 60 -20, 30 -60, -10 -50 C -40 -30, -50 0, -40 20 Z" fill="#06b6d4" opacity="0.3" style={{ mixBlendMode: 'screen' }} />
      </g>

      {/* Swirling Vector Flows */}
      <g fill="none" stroke="#38bdf8" strokeLinecap="round" opacity="0.8">
        <path d="M -40 -40 C -10 -50, 20 -20, 40 -30" strokeWidth="2.5" />
        <path d="M -50 -10 C -20 -10, 10 20, 40 10" strokeWidth="3" />
        <path d="M -40 30 C -20 40, 20 50, 40 30" strokeWidth="2" />
        <path d="M -10 -50 C 10 -20, -10 20, 10 50" strokeWidth="1.5" strokeDasharray="4 4" />
      </g>

      {/* Velocity Field (Arrows) */}
      <g stroke="rgba(255,255,255,0.4)" strokeWidth="1.2" fill="none">
        {/* Row 1 */}
        <path d="M -50 -60 L -45 -55 M -45 -55 L -48 -54 M -45 -55 L -46 -58" />
        <path d="M -20 -60 L -12 -58 M -12 -58 L -15 -56 M -12 -58 L -13 -61" />
        <path d="M 10 -60 L 20 -55 M 20 -55 L 17 -53 M 20 -55 L 18 -58" />
        {/* Row 2 */}
        <path d="M -55 -30 L -45 -25 M -45 -25 L -48 -24 M -45 -25 L -47 -28" />
        <path d="M -15 -30 L -5 -20 M -5 -20 L -8 -21 M -5 -20 L -6 -24" />
        <path d="M 15 -30 L 25 -35 M 25 -35 L 22 -36 M 25 -35 L 24 -32" />
        {/* Row 3 */}
        <path d="M -60 0 L -50 -5 M -50 -5 L -52 -3 M -50 -5 L -53 -7" />
        <path d="M -20 0 L -10 10 M -10 10 L -12 8 M -10 10 L -10 6" />
        <path d="M 20 0 L 30 5 M 30 5 L 27 6 M 30 5 L 28 2" />
      </g>
      
      <circle cx="-10" cy="10" r="4" fill="#fff" opacity="0.8" />
      <circle cx="25" cy="-20" r="3" fill="#fff" opacity="0.6" />
      <circle cx="-35" cy="-25" r="5" fill="#7dd3fc" opacity="0.7" filter="blur(1px)" />
    </g>
  </svg>
);

const WaterSurfaceWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="waterTop" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#0ea5e9" />
        <stop offset="100%" stopColor="#0369a1" />
      </linearGradient>
      <linearGradient id="waterLeft" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#0284c7" />
        <stop offset="100%" stopColor="#0c4a6e" />
      </linearGradient>
      <linearGradient id="waterRight" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#0369a1" />
        <stop offset="100%" stopColor="#082f49" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 130)">
      {/* Base Depth */}
      <path d="M -80 -20 L 0 20 L 80 -20 L 80 30 L 0 70 L -80 30 Z" fill="#082f49" opacity="0.5" />
      
      {/* 3D Block Faces */}
      <polygon points="-80,-20 0,20 0,60 -80,20" fill="url(#waterLeft)" />
      <polygon points="0,20 80,-20 80,20 0,60" fill="url(#waterRight)" />
      
      {/* Surface Displacement */}
      <path d="M -80 -20 Q -60 -35 -40 -20 T 0 -20 T 40 -30 T 80 -20 L 0 20 Z" fill="url(#waterTop)" />
      <path d="M -80 -20 L 0 20 L 80 -20 L 40 -40 Q 20 -45 0 -40 T -40 -30 Z" fill="url(#waterTop)" opacity="0.8" />
      
      {/* Caustics Grid (Warped) */}
      <g stroke="#7dd3fc" strokeWidth="1.2" opacity="0.4" fill="none">
        <path d="M -60 -25 Q -40 -35 -20 -15 T 20 -15 T 60 -25" />
        <path d="M -40 -15 Q -20 -25 0 -5 T 40 -5" />
        <path d="M -20 -5 Q 0 -15 20 5" />
        <path d="M -60 -25 Q -40 -10 -20 -5 T 20 15" />
        <path d="M -40 -35 Q -20 -20 0 -15 T 40 5" />
      </g>
      
      {/* Ripple Rings */}
      <ellipse cx="-10" cy="-20" rx="30" ry="12" fill="none" stroke="#bae6fd" strokeWidth="2" opacity="0.8" />
      <ellipse cx="-10" cy="-20" rx="45" ry="18" fill="none" stroke="#bae6fd" strokeWidth="1" opacity="0.5" />
      
      {/* Splash */}
      <circle cx="-10" cy="-35" r="3" fill="#e0f2fe" />
      <circle cx="-18" cy="-25" r="2" fill="#bae6fd" />
      <circle cx="-2" cy="-28" r="2.5" fill="#e0f2fe" />
      <circle cx="-10" cy="-45" r="1.5" fill="#ffffff" />
    </g>
  </svg>
);


const ClothSimWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="clothSoftBg" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#2e1065" />
        <stop offset="100%" stopColor="#172554" />
      </linearGradient>
      <radialGradient id="sphereBump" cx="40%" cy="40%" r="60%">
        <stop offset="0%" stopColor="#60a5fa" stopOpacity="0.4" />
        <stop offset="100%" stopColor="#1e3a8a" stopOpacity="0.0" />
      </radialGradient>
    </defs>
    <g transform="translate(120, 120)">
      {/* Background Frame */}
      <circle cx="0" cy="0" r="100" fill="url(#clothSoftBg)" stroke="#3b82f6" strokeWidth="2" strokeOpacity="0.3" />
      
      {/* Underlying Collision Sphere */}
      <circle cx="0" cy="20" r="45" fill="url(#sphereBump)" stroke="#3b82f6" strokeWidth="1" strokeDasharray="2 4" />
      
      {/* Draped Cloth (Simulated wrapping over sphere) */}
      <path d="M -60 -50 
               C -40 -60, 40 -60, 60 -50
               C 80 0, 50 60, 0 70
               C -50 60, -80 0, -60 -50 Z" 
            fill="#8b5cf6" opacity="0.85" stroke="#a78bfa" strokeWidth="1.5" />
            
      {/* Grid mapping for the draped surface */}
      <g stroke="#ddd6fe" strokeWidth="1" fill="none" opacity="0.6">
        {/* Latitudes */}
        <path d="M -62 -30 Q 0 -45, 62 -30" />
        <path d="M -65 -10 Q 0 -35, 65 -10" />
        <path d="M -62 15 Q 0 -20, 62 15" />
        <path d="M -50 40 Q 0 -5, 50 40" />
        
        {/* Longitudes */}
        <path d="M -40 -53 Q -60 10, -25 58" />
        <path d="M -20 -58 Q -30 10, 0 70" />
        <path d="M 0 -60 Q 0 10, 0 70" />
        <path d="M 20 -58 Q 30 10, 0 70" />
        <path d="M 40 -53 Q 60 10, 25 58" />
      </g>
      
      {/* Wrinkles/Tension Highlights */}
      <path d="M -40 -53 Q -10 -10, -20 20" stroke="#f3e8ff" strokeWidth="2" fill="none" opacity="0.5" strokeLinecap="round" />
      <path d="M 40 -53 Q 10 -10, 20 20" stroke="#f3e8ff" strokeWidth="2" fill="none" opacity="0.5" strokeLinecap="round" />
      
      {/* Anchor Points */}
      <circle cx="-60" cy="-50" r="4" fill="#fbbf24" />
      <circle cx="60" cy="-50" r="4" fill="#fbbf24" />
    </g>
  </svg>
);

const ClothSimWidget2 = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="curtainBg" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#4c0519" />
        <stop offset="100%" stopColor="#020617" />
      </linearGradient>
      <linearGradient id="silk" x1="0" y1="0" x2="1" y2="0">
        <stop offset="0%" stopColor="#be123c" />
        <stop offset="30%" stopColor="#fb7185" />
        <stop offset="60%" stopColor="#e11d48" />
        <stop offset="80%" stopColor="#f43f5e" />
        <stop offset="100%" stopColor="#9f1239" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 120)">
      <rect x="-100" y="-100" width="200" height="200" rx="16" fill="url(#curtainBg)" stroke="#9f1239" strokeWidth="2" />
      
      {/* Rod */}
      <rect x="-80" y="-70" width="160" height="8" rx="4" fill="#d4d4d8" />
      
      {/* Silk Curtain Folds */}
      <path d="M -70 -62 
               C -70 0, -40 30, -50 80
               L -20 80
               C -10 30, -40 0, -40 -62 Z" fill="url(#silk)" />
               
      <path d="M -40 -62 
               C -40 0, -10 30, -20 80
               L 10 80
               C 20 30, -10 0, -10 -62 Z" fill="url(#silk)" opacity="0.9" />

      <path d="M -10 -62 
               C -10 0, 20 30, 10 80
               L 40 80
               C 50 30, 20 0, 20 -62 Z" fill="url(#silk)" opacity="0.95" />

      <path d="M 20 -62 
               C 20 0, 50 30, 40 80
               L 70 80
               C 80 30, 50 0, 50 -62 Z" fill="url(#silk)" />
               
      {/* Rings on rod */}
      <ellipse cx="-55" cy="-66" rx="4" ry="8" fill="none" stroke="#fbbf24" strokeWidth="2" />
      <ellipse cx="-25" cy="-66" rx="4" ry="8" fill="none" stroke="#fbbf24" strokeWidth="2" />
      <ellipse cx="5" cy="-66" rx="4" ry="8" fill="none" stroke="#fbbf24" strokeWidth="2" />
      <ellipse cx="35" cy="-66" rx="4" ry="8" fill="none" stroke="#fbbf24" strokeWidth="2" />
      <ellipse cx="60" cy="-66" rx="4" ry="8" fill="none" stroke="#fbbf24" strokeWidth="2" />
    </g>
  </svg>
);

const ClothSimWidget3 = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="flagBg" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#0f172a" />
        <stop offset="100%" stopColor="#020617" />
      </linearGradient>
      <linearGradient id="flagFill" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#0ea5e9" />
        <stop offset="100%" stopColor="#0369a1" />
      </linearGradient>
    </defs>
    <g transform="translate(120, 120)">
      <rect x="-100" y="-100" width="200" height="200" rx="16" fill="url(#flagBg)" />
      
      {/* Flagpole */}
      <rect x="-60" y="-80" width="6" height="160" rx="3" fill="#cbd5e1" />
      <circle cx="-57" cy="-80" r="6" fill="#fbbf24" />
      
      {/* Wind Flow Lines */}
      <path d="M -80 -40 Q -40 -60, 80 -30" fill="none" stroke="#38bdf8" strokeWidth="1" strokeDasharray="4 8" opacity="0.3" />
      <path d="M -80 0 Q -20 -20, 80 10" fill="none" stroke="#38bdf8" strokeWidth="1" strokeDasharray="6 8" opacity="0.3" />
      <path d="M -80 40 Q 0 20, 80 50" fill="none" stroke="#38bdf8" strokeWidth="1" strokeDasharray="4 8" opacity="0.3" />

      {/* Flag */}
      <path d="M -54 -60 
               C -20 -80, 20 -40, 70 -60 
               L 70 20 
               C 20 40, -20 0, -54 20 Z" fill="url(#flagFill)" stroke="#7dd3fc" strokeWidth="1" />
               
      {/* Wind Ripples (Highlights & Shadows) */}
      <path d="M -54 -60 C -20 -80, 20 -40, 70 -60" fill="none" stroke="#ffffff" strokeWidth="2" opacity="0.5" />
      <path d="M -20 -60 C 0 -40, 20 -40, 40 -50 L 40 25 C 20 35, 0 35, -20 15 Z" fill="#0c4a6e" opacity="0.4" />
      <path d="M 10 -45 C 30 -35, 50 -45, 65 -55 L 65 20 C 50 30, 30 40, 10 30 Z" fill="#38bdf8" opacity="0.4" />
      
      <g stroke="#bae6fd" strokeWidth="1" opacity="0.5">
        <path d="M -40 -58 Q -20 -30, -40 18" />
        <path d="M -20 -58 Q 0 -30, -20 22" />
        <path d="M 0 -52 Q 20 -20, 0 28" />
        <path d="M 20 -45 Q 40 -10, 20 32" />
        <path d="M 40 -50 Q 60 -20, 40 28" />
      </g>
    </g>
  </svg>
);

const HeightmapTerrainWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="bgHeat" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="#18181b" />
        <stop offset="100%" stopColor="#09090b" />
      </linearGradient>
      <linearGradient id="mountain" x1="0" y1="1" x2="0" y2="0">
        <stop offset="0%" stopColor="#1e3a8a" />
        <stop offset="20%" stopColor="#064e3b" />
        <stop offset="50%" stopColor="#78350f" />
        <stop offset="80%" stopColor="#7f1d1d" />
        <stop offset="100%" stopColor="#f87171" />
      </linearGradient>
      <filter id="layerShadow">
        <feDropShadow dx="0" dy="4" stdDeviation="4" floodColor="#000" floodOpacity="0.5" />
      </filter>
    </defs>
    <g transform="translate(120, 120)">
      <rect x="-100" y="-100" width="200" height="200" rx="20" fill="url(#bgHeat)" stroke="rgba(255,255,255,0.1)" strokeWidth="2" />
      
      {/* 3D Topo Map Layers */}
      <g transform="translate(0, 30)">
        {/* Base Layer */}
        <ellipse cx="0" cy="0" rx="70" ry="30" fill="#1e3a8a" filter="url(#layerShadow)" stroke="#3b82f6" strokeWidth="1" />
        {/* Layer 2 */}
        <ellipse cx="5" cy="-10" rx="55" ry="22" fill="#064e3b" filter="url(#layerShadow)" stroke="#10b981" strokeWidth="1" />
        {/* Layer 3 */}
        <ellipse cx="-10" cy="-20" rx="40" ry="16" fill="#78350f" filter="url(#layerShadow)" stroke="#f59e0b" strokeWidth="1" />
        {/* Layer 4 */}
        <ellipse cx="-5" cy="-30" rx="25" ry="10" fill="#7f1d1d" filter="url(#layerShadow)" stroke="#ef4444" strokeWidth="1" />
        {/* Peak */}
        <ellipse cx="-2" cy="-40" rx="10" ry="4" fill="#f87171" filter="url(#layerShadow)" stroke="#fca5a5" strokeWidth="1" />
      </g>
      
      {/* Sampling Reticle */}
      <circle cx="-2" cy="-10" r="12" fill="none" stroke="#22d3ee" strokeWidth="2" strokeDasharray="4 2" />
      <line x1="-2" y1="-22" x2="-2" y2="2" stroke="#22d3ee" strokeWidth="1" />
      <line x1="-14" y1="-10" x2="10" y2="-10" stroke="#22d3ee" strokeWidth="1" />
      
      <path d="M -2 -10 L 30 -50" stroke="#22d3ee" strokeWidth="1.5" strokeDasharray="2 2" />
      <rect x="30" y="-65" width="45" height="20" rx="4" fill="#0f172a" stroke="#22d3ee" strokeWidth="1" />
      <text x="52.5" y="-51" fill="#22d3ee" fontSize="10" fontFamily="sans-serif" fontWeight="bold" textAnchor="middle">H: 240</text>
    </g>
  </svg>
);

const TexturePaintWidget = () => (
  <svg width="240" height="240" viewBox="0 0 240 240" className="drop-shadow-2xl hover:scale-105 transition-transform duration-500 ease-out">
    <defs>
      <linearGradient id="canvasGrad" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#f3f4f6" />
        <stop offset="100%" stopColor="#d1d5db" />
      </linearGradient>
      <pattern id="uvGrid" width="20" height="20" patternUnits="userSpaceOnUse" patternTransform="rotate(15)">
        <rect width="20" height="20" fill="none" stroke="#9ca3af" strokeWidth="0.5" />
        <rect width="10" height="10" fill="#e5e7eb" opacity="0.5" />
        <rect x="10" y="10" width="10" height="10" fill="#e5e7eb" opacity="0.5" />
      </pattern>
    </defs>
    <g transform="translate(120, 120)">
      {/* Tilted Canvas */}
      <g transform="rotate(-15) skewX(20)">
        {/* Canvas Thickness */}
        <rect x="-60" y="-60" width="120" height="120" rx="4" fill="#9ca3af" transform="translate(0, 5)" />
        <rect x="-60" y="-60" width="120" height="120" rx="4" fill="#4b5563" transform="translate(5, 5)" />
        
        {/* Canvas Surface */}
        <rect x="-60" y="-60" width="120" height="120" rx="4" fill="url(#canvasGrad)" stroke="#6b7280" strokeWidth="1" />
        <rect x="-60" y="-60" width="120" height="120" rx="4" fill="url(#uvGrid)" />
        
        {/* Paint Splats (Vector art) */}
        {/* Yellow Stroke */}
        <path d="M -40 40 Q -10 60, 40 20" fill="none" stroke="#eab308" strokeWidth="12" strokeLinecap="round" />
        
        {/* Cyan Splatter */}
        <path d="M -20 -20 Q -40 -30 -10 -50 Q 10 -30 20 -10 Q 0 0 -20 -20 Z" fill="#06b6d4" opacity="0.9" />
        <circle cx="-35" cy="-45" r="4" fill="#06b6d4" />
        <circle cx="15" cy="-35" r="3" fill="#06b6d4" />
        <circle cx="0" cy="-55" r="2" fill="#06b6d4" />

        {/* Magenta Thick Stroke */}
        <path d="M -30 10 Q 0 -20, 30 10 Q 10 30, 20 40 Q 40 10, 20 -20 Q -20 -30, -30 10 Z" fill="#ec4899" />
      </g>
      
      {/* Hovering Stylus */}
      <g transform="translate(30, -30) rotate(-35)">
        <filter id="stylusShadow">
          <feDropShadow dx="-15" dy="15" stdDeviation="6" floodColor="#000" floodOpacity="0.3" />
        </filter>
        <g filter="url(#stylusShadow)">
          {/* Stylus Body */}
          <rect x="-8" y="-50" width="16" height="60" rx="3" fill="#1f2937" stroke="#374151" strokeWidth="1.5" />
          {/* Grip Lines */}
          <line x1="-8" y1="-20" x2="8" y2="-20" stroke="#374151" strokeWidth="2" />
          <line x1="-8" y1="-15" x2="8" y2="-15" stroke="#374151" strokeWidth="2" />
          <line x1="-8" y1="-10" x2="8" y2="-10" stroke="#374151" strokeWidth="2" />
          
          {/* Tip */}
          <polygon points="-6,10 6,10 0,30" fill="#d1d5db" />
          <polygon points="-3,20 3,20 0,30" fill="#ec4899" /> {/* Paint on tip */}
          
          {/* Highlight */}
          <rect x="3" y="-45" width="2" height="50" rx="1" fill="#fff" opacity="0.2" />
        </g>
      </g>
    </g>
  </svg>
);



const IconWrapper = ({ children, name }: { children: React.ReactNode, name: string }) => {
  const [copied, setCopied] = useState(false);
  const containerRef = useRef<HTMLDivElement>(null);

  const handleCopy = () => {
    if (containerRef.current) {
      const svg = containerRef.current.innerHTML;
      navigator.clipboard.writeText(svg).then(() => {
        setCopied(true);
        setTimeout(() => setCopied(false), 2000);
      });
    }
  };

  return (
    <div className="flex flex-col items-center gap-4 group cursor-pointer" onClick={handleCopy}>
      <div className="relative">
        <div ref={containerRef}>
          {children}
        </div>
        <div className={`absolute inset-0 flex items-center justify-center bg-black/50 rounded-2xl opacity-0 transition-opacity duration-200 ${copied ? 'opacity-100' : 'group-hover:opacity-100'}`}>
          <span className="text-white font-medium px-3 py-1 bg-black/80 rounded-full text-sm">
            {copied ? 'Copied SVG!' : 'Click to Copy'}
          </span>
        </div>
      </div>
      <span className="text-gray-400 font-medium text-sm tracking-wide bg-[#1a1a1a] px-4 py-1.5 rounded-full border border-[#2a2a2a] group-hover:text-white transition-colors duration-200">
        {name}
      </span>
    </div>
  );
};

export default function App() {
  return (
    <div className="min-h-screen bg-[#111111] flex flex-wrap items-center justify-center gap-16 p-8">
      <IconWrapper name="Document"><DocumentWidget /></IconWrapper>
      <IconWrapper name="Folder"><FolderWidget /></IconWrapper>
      <IconWrapper name="Sync"><SyncWidget /></IconWrapper>
      <IconWrapper name="Image"><ImageWidget /></IconWrapper>
      <IconWrapper name="Image Widget Variant2"><ImageWidgetVariant2 /></IconWrapper>
      <IconWrapper name="Image Widget Variant3"><ImageWidgetVariant3 /></IconWrapper>
      <IconWrapper name="Image Widget Variant4"><ImageWidgetVariant4 /></IconWrapper>
      <IconWrapper name="Camera"><CameraWidget /></IconWrapper>
      <IconWrapper name="Video Camera"><VideoCameraWidget /></IconWrapper>
      <IconWrapper name="Instant Camera"><InstantCameraWidget /></IconWrapper>
      <IconWrapper name="Cinematic Camera"><CinematicCameraWidget /></IconWrapper>
      <IconWrapper name="Drone Camera"><DroneCameraWidget /></IconWrapper>
      <IconWrapper name="Vintage Projector"><VintageProjectorWidget /></IconWrapper>
      <IconWrapper name="Director Monitor"><DirectorMonitorWidget /></IconWrapper>
      <IconWrapper name="Studio Lighting"><StudioLightingWidget /></IconWrapper>
      <IconWrapper name="Gimbal Stabilizer"><GimbalStabilizerWidget /></IconWrapper>
      <IconWrapper name="Audio Boom"><AudioBoomWidget /></IconWrapper>
      <IconWrapper name="Clapboard"><ClapboardWidget /></IconWrapper>
      <IconWrapper name="Action Camera"><ActionCameraWidget /></IconWrapper>
      <IconWrapper name="Camera Stack"><CameraStackWidget /></IconWrapper>
      <IconWrapper name="Video Player"><VideoPlayerWidget /></IconWrapper>
      <IconWrapper name="Film Strip"><FilmStripWidget /></IconWrapper>
      <IconWrapper name="Meeting Grid"><MeetingGridWidget /></IconWrapper>
      <IconWrapper name="Live Broadcast"><LiveBroadcastWidget /></IconWrapper>
      <IconWrapper name="Old Gamepad Classic"><OldGamepadClassicWidget /></IconWrapper>
      <IconWrapper name="Sun"><SunWidget /></IconWrapper>
      <IconWrapper name="Sun Sundial"><SunSundialWidget /></IconWrapper>
      <IconWrapper name="Sun Orrery"><SunOrreryWidget /></IconWrapper>
      <IconWrapper name="Modern Sunset"><ModernSunsetWidget /></IconWrapper>
      <IconWrapper name="Sky"><SkyWidget /></IconWrapper>
      <IconWrapper name="Sky Astrolabe"><SkyAstrolabeWidget /></IconWrapper>
      <IconWrapper name="Sky Sextant"><SkySextantWidget /></IconWrapper>
      <IconWrapper name="Modern Cloudscape"><ModernCloudscapeWidget /></IconWrapper>
      <IconWrapper name="Atmosphere"><AtmosphereWidget /></IconWrapper>
      <IconWrapper name="Atmosphere Barometer"><AtmosphereBarometerWidget /></IconWrapper>
      <IconWrapper name="Atmosphere Anemometer"><AtmosphereAnemometerWidget /></IconWrapper>
      <IconWrapper name="Modern Aurora Atmosphere"><ModernAuroraAtmosphereWidget /></IconWrapper>
      <IconWrapper name="Fabric"><FabricWidget /></IconWrapper>
      <IconWrapper name="Metal"><MetalWidget /></IconWrapper>
      <IconWrapper name="Plastic"><PlasticWidget /></IconWrapper>
      <IconWrapper name="Wood"><WoodWidget /></IconWrapper>
      <IconWrapper name="Ring Light"><RingLightWidget /></IconWrapper>
      <IconWrapper name="Spotlight"><SpotlightWidget /></IconWrapper>
      <IconWrapper name="L E D Panel"><LEDPanelWidget /></IconWrapper>
      <IconWrapper name="Flash Speedlight"><FlashSpeedlightWidget /></IconWrapper>
      <IconWrapper name="Light Bulb"><LightBulbWidget /></IconWrapper>
      <IconWrapper name="Point Light"><PointLightWidget /></IconWrapper>
      <IconWrapper name="Cinematic Light"><CinematicLightWidget /></IconWrapper>
      <IconWrapper name="Area Light"><AreaLightWidget /></IconWrapper>
      <IconWrapper name="Single L E D"><SingleLEDWidget /></IconWrapper>
      <IconWrapper name="I E S Light"><IESLightWidget /></IconWrapper>
      <IconWrapper name="Desk Lamp"><DeskLampWidget /></IconWrapper>
      <IconWrapper name="Lantern"><LanternWidget /></IconWrapper>
      <IconWrapper name="Candle"><CandleWidget /></IconWrapper>
      <IconWrapper name="Disco Ball"><DiscoBallWidget /></IconWrapper>
      <IconWrapper name="Lava Lamp"><LavaLampWidget /></IconWrapper>
          <IconWrapper name="Mesh"><MeshWidget /></IconWrapper>
      <IconWrapper name="Material"><MaterialWidget /></IconWrapper>
      <IconWrapper name="Particle"><ParticleWidget /></IconWrapper>
      <IconWrapper name="Rigging"><RiggingWidget /></IconWrapper>
      <IconWrapper name="Script Node"><ScriptNodeWidget /></IconWrapper>
      <IconWrapper name="Collider"><ColliderWidget /></IconWrapper>
          <IconWrapper name="Terrain"><TerrainWidget /></IconWrapper>
      <IconWrapper name="Audio Emitter"><AudioEmitterWidget /></IconWrapper>
      <IconWrapper name="Animation State"><AnimationStateWidget /></IconWrapper>
            <IconWrapper name="Physics Material"><PhysicsMaterialWidget /></IconWrapper>
          <IconWrapper name="V F X Graph"><VFXGraphWidget /></IconWrapper>
      <IconWrapper name="Decal"><DecalWidget /></IconWrapper>
      <IconWrapper name="L O D Group"><LODGroupWidget /></IconWrapper>
      <IconWrapper name="U I Canvas"><UICanvasWidget /></IconWrapper>
            <IconWrapper name="Timeline"><TimelineWidget /></IconWrapper>
          <IconWrapper name="Tilemap"><TilemapWidget /></IconWrapper>
      <IconWrapper name="Sprite Sheet"><SpriteSheetWidget /></IconWrapper>
      <IconWrapper name="Gizmo"><GizmoWidget /></IconWrapper>
      <IconWrapper name="Light Probe"><LightProbeWidget /></IconWrapper>
      <IconWrapper name="Terrain Brush"><TerrainBrushWidget /></IconWrapper>
      <IconWrapper name="Audio Mixer"><AudioMixerWidget /></IconWrapper>
          <IconWrapper name="Profiler"><ProfilerWidget /></IconWrapper>
      <IconWrapper name="Scene Hierarchy"><SceneHierarchyWidget /></IconWrapper>
      <IconWrapper name="Color Picker"><ColorPickerWidget /></IconWrapper>
      <IconWrapper name="Terminal"><TerminalWidget /></IconWrapper>
      <IconWrapper name="Build"><BuildWidget /></IconWrapper>
      <IconWrapper name="X R"><XRWidget /></IconWrapper>
          <IconWrapper name="Camera Frustum"><CameraFrustumWidget /></IconWrapper>
      <IconWrapper name="Behavior Tree"><BehaviorTreeWidget /></IconWrapper>
      <IconWrapper name="U V Editor"><UVEditorWidget /></IconWrapper>
      <IconWrapper name="Gamepad Input"><GamepadInputWidget /></IconWrapper>
      <IconWrapper name="Post Processing"><PostProcessingWidget /></IconWrapper>
      <IconWrapper name="Spline Path"><SplinePathWidget /></IconWrapper>
          <IconWrapper name="Particle Emitter"><ParticleEmitterWidget /></IconWrapper>
      <IconWrapper name="Physics Rigidbody"><PhysicsRigidbodyWidget /></IconWrapper>
      <IconWrapper name="Animation Curve"><AnimationCurveWidget /></IconWrapper>
                  <IconWrapper name="Shader Node"><ShaderNodeWidget /></IconWrapper>
          <IconWrapper name="Nav Mesh"><NavMeshWidget /></IconWrapper>
      <IconWrapper name="Raycast"><RaycastWidget /></IconWrapper>
      <IconWrapper name="Audio Source"><AudioSourceWidget /></IconWrapper>
      <IconWrapper name="I K Rig"><IKRigWidget /></IconWrapper>
          <IconWrapper name="Fluid Sim"><FluidSimWidget /></IconWrapper>
      <IconWrapper name="Water Surface"><WaterSurfaceWidget /></IconWrapper>
      <IconWrapper name="Cloth Sim"><ClothSimWidget /></IconWrapper>
      <IconWrapper name="Heightmap Terrain"><HeightmapTerrainWidget /></IconWrapper>
      <IconWrapper name="Texture Paint"><TexturePaintWidget /></IconWrapper>
    </div>
  );
}
