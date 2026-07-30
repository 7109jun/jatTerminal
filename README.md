# jatterminal 
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">
  <defs>
    <!-- 네온 글로우 필터 -->
    <filter id="neonGlow" x="-50%" y="-50%" width="200%" height="200%">
      <feGaussianBlur in="SourceGraphic" stdDeviation="8" result="blur1" />
      <feGaussianBlur in="SourceGraphic" stdDeviation="16" result="blur2" />
      <feMerge>
        <feMergeNode in="blur2" />
        <feMergeNode in="blur1" />
        <feMergeNode in="SourceGraphic" />
      </feMerge>
    </filter>

    <!-- 커서 블럭의 빛 번짐 효과 -->
    <filter id="cursorBloom" x="-50%" y="-50%" width="200%" height="200%">
      <feGaussianBlur in="SourceGraphic" stdDeviation="12" result="blur" />
      <feMerge>
        <feMergeNode in="blur" />
        <feMergeNode in="SourceGraphic" />
      </feMerge>
    </filter>

    <!-- 스캔라인 패턴 -->
    <pattern id="scanlines" width="4" height="4" patternUnits="userSpaceOnUse">
      <line x1="0" y1="0" x2="4" y2="0" stroke="#ffffff" stroke-width="1" opacity="0.08" />
    </pattern>
    
    <!-- 중앙 하단으로 갈수록 진해지는 그라데이션 (CRT 느낌) -->
    <linearGradient id="screenFade" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#ffffff" stop-opacity="0" />
      <stop offset="100%" stop-color="#ffffff" stop-opacity="0.15" />
    </linearGradient>
  </defs>

  <!-- 배경 -->
  <rect width="100%" height="100%" fill="#0a0a0a" />

  <!-- 민트색 네온 테두리 그룹 -->
  <g filter="url(#neonGlow)">
    <!-- 바깥쪽 밝은 테두리 -->
    <rect x="50" y="50" width="412" height="412" rx="45" ry="45" 
          fill="none" stroke="#69f0ae" stroke-width="12" />
    <!-- 안쪽 약간 어두운 테두리 (입체감) -->
    <rect x="56" y="56" width="400" height="400" rx="40" ry="40" 
          fill="none" stroke="#00e676" stroke-width="4" opacity="0.6" />
  </g>

  <!-- 터미널 창 본문 -->
  <rect x="75" y="75" width="362" height="362" rx="25" ry="25" fill="#1e1e1e" />

  <!-- 상단 타이틀 바 -->
  <path d="M75 100 Q75 75 100 75 L412 75 Q437 75 437 100 L437 135 L75 135 Z" fill="#3c3c3c" />

  <!-- 윈도우 컨트롤 버튼 (좌측 상단) -->
  <circle cx="115" cy="105" r="7" fill="#b0b0b0" /> <!-- 닫기/최소화 등 -->
  <circle cx="145" cy="105" r="7" fill="#808080" />
  <circle cx="175" cy="105" r="7" fill="#404040" />

  <!-- 메인 화면 영역 (스캔라인 및 페이드 오버레이 적용) -->
  <rect x="75" y="135" width="362" height="302" rx="0" ry="0" fill="#121212" />
  <rect x="75" y="135" width="362" height="302" fill="url(#scanlines)" />
  <rect x="75" y="135" width="362" height="302" fill="url(#screenFade)" />

  <!-- 터미널 프롬프트 ">_" -->
  <g fill="#ffffff">
    <!-- ">" 기호 -->
    <polygon points="180,245 215,268 180,291 195,268" />
    <!-- "_" 기호 -->
    <rect x="230" y="280" width="25" height="6" />
  </g>

  <!-- 커서 블럭 (빛 번짐 효과 포함) -->
  <g filter="url(#cursorBloom)">
    <rect x="275" y="235" width="45" height="55" fill="#ffffff" opacity="0.9" />
  </g>
  <!-- 커서 블럭 핵심 (더 선명하게) -->
  <rect x="275" y="235" width="45" height="55" fill="#ffffff" />

  <!-- 하단 텍스트 "jat Terminal" -->
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
