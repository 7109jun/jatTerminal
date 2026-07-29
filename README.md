<svg width="400" height="200" viewBox="0 0 400 200" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <!-- 연두색 네온 글로우 효과 -->
    <filter id="limeGlow" x="-50%" y="-50%" width="200%" height="200%">
      <feGaussianBlur in="SourceGraphic" stdDeviation="8" result="blur1" />
      <feGaussianBlur in="SourceGraphic" stdDeviation="15" result="blur2" />
      <feMerge>
        <feMergeNode in="blur2" />
        <feMergeNode in="blur1" />
        <feMergeNode in="SourceGraphic" />
      </feMerge>
    </filter>
    
    <!-- 텍스트 가독성을 위한 미세 글로우 -->
    <filter id="textSoftGlow" x="-20%" y="-20%" width="140%" height="140%">
      <feGaussianBlur in="SourceGraphic" stdDeviation="1.5" result="blur" />
      <feMerge>
        <feMergeNode in="blur" />
        <feMergeNode in="SourceGraphic" />
      </feMerge>
    </filter>
  </defs>

  <!-- 전체 카드 배경 (GitHub 다크모드 대응) -->
  <rect width="400" height="200" rx="16" fill="#0A0A0A" />

  <!-- 터미널 창 프레임: 연두색(#78FFAF) 테두리 + 글로우 -->
  <rect x="50" y="35" width="300" height="110" rx="12" 
        fill="#1E1E1E" stroke="#78FFAF" stroke-width="4" 
        filter="url(#limeGlow)" />
  
  <!-- 프레임 내부 경계선 (글로우로 인해 흐려진 외곽 보완) -->
  <rect x="50" y="35" width="300" height="110" rx="12" 
        fill="none" stroke="#78FFAF" stroke-width="1.5" opacity="0.9" />

  <!-- 타이틀바 (회색 #373737) -->
  <path d="M50 47 Q50 35 62 35 L338 35 Q350 35 350 47 L350 60 L50 60 Z" 
        fill="#373737" />

  <!-- 윈도우 컨트롤 버튼 (회색 도트) -->
  <circle cx="72" cy="47.5" r="4.5" fill="#8A8F98" />
  <circle cx="90" cy="47.5" r="4.5" fill="#8A8F98" />
  <circle cx="108" cy="47.5" r="4.5" fill="#8A8F98" />

  <!-- 중앙 프롬프트: 흰색(EBEBEB) >_ 기호 + 블록 커서 -->
  <g filter="url(#textSoftGlow)">
    <text x="140" y="105" font-family="'Courier New', Courier, monospace" 
          font-size="48" font-weight="bold" fill="#EBEBEB">&gt;_</text>
    <rect x="215" y="72" width="28" height="36" rx="3" fill="#EBEBEB" />
  </g>

  <!-- 하단 워드마크 -->
  <text x="200" y="175" text-anchor="middle" 
        font-family="'Segoe UI', Arial, sans-serif" font-size="22" 
        font-weight="bold" fill="#EBEBEB" letter-spacing="1">jat Terminal</text>
</svg>
# jatTeminal
### jat은
> 터미널이며
>
> 심심해서 시작했습니다
>
> 모든 버전 다 공개하며 라이선스는 Licence.md를 확인하세요(링크 https://github.com/7109jun/jatTerminal/blob/main/Licence.md)
>
## jat은 자동화 도구입니다
#### C++ 기반이며 .cpp로 저장하면 될겁니다.
