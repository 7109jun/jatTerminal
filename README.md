# jatterminal 
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">
  <defs>
    <pattern id="scanlines" width="4" height="4" patternUnits="userSpaceOnUse">
      <line x1="0" y1="0" x2="4" y2="0" stroke="#ffffff" stroke-width="1" opacity="0.08"/>
    </pattern>
    <linearGradient id="screenFade" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#ffffff" stop-opacity="0"/>
      <stop offset="100%" stop-color="#ffffff" stop-opacity="0.12"/>
    </linearGradient>
    <style>
      .neon-border { filter: drop-shadow(0 0 6px #69f0ae) drop-shadow(0 0 14px #69f0ae) drop-shadow(0 0 28px #00e676); }
      .cursor-glow { filter: drop-shadow(0 0 8px #fff) drop-shadow(0 0 18px #fff) drop-shadow(0 0 32px #aaffcc); }
    </style>
  </defs>

  <rect width="100%" height="100%" fill="#0a0a0a"/>
  
  <g class="neon-border">
    <rect x="50" y="50" width="412" height="412" rx="45" ry="45" fill="none" stroke="#69f0ae" stroke-width="10"/>
    <rect x="56" y="56" width="400" height="400" rx="40" ry="40" fill="none" stroke="#00e676" stroke-width="3" opacity="0.5"/>
  </g>

  <rect x="75" y="75" width="362" height="362" rx="25" ry="25" fill="#1e1e1e"/>
  <path d="M75 100 Q75 75 100 75 L412 75 Q437 75 437 100 L437 135 L75 135 Z" fill="#3c3c3c"/>
  
  <circle cx="115" cy="105" r="7" fill="#b0b0b0"/>
  <circle cx="145" cy="105" r="7" fill="#808080"/>
  <circle cx="175" cy="105" r="7" fill="#404040"/>

  <rect x="75" y="135" width="362" height="302" fill="#121212"/>
  <rect x="75" y="135" width="362" height="302" fill="url(#scanlines)"/>
  <rect x="75" y="135" width="362" height="302" fill="url(#screenFade)"/>

  <g fill="#ffffff">
    <polygon points="180,245 215,268 180,291 195,268"/>
    <rect x="230" y="280" width="25" height="6"/>
  </g>

  <g class="cursor-glow">
    <rect x="275" y="235" width="45" height="55" fill="#ffffff"/>
  </g>

  <text x="256" y="485" font-family="'Segoe UI', Roboto, Helvetica, Arial, sans-serif" 
        font-size="38" font-weight="500" fill="#ffffff" text-anchor="middle" letter-spacing="0.5">
    jat Terminal
  </text>
</svg>
> 셀 터미널
>
> 둘의 기능을 둘다 하며
>
>  Powershell같은걸 만들어보고 싶어서 만든것.
라이선스는 licence.md를 확인하세요.https://creativecommons.org/licenses/by/4.0/deed.ko 또는 이것을 확인하십시오.
모든건 오픈소스로 공개하며. .cpp파일로 됩니다. 실행파일은 아니니.
VScode g++ 컴파일러기로 하시기 바랍니다.
### [그리고 jatTerminal은 당신이 한 일에 대한것을 책임지지 않습니다]
# 문법은 추후 공개하겠습니다.  
