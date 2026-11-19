# Identidade visual — resumo do manual v1.0

O manual completo é o PDF `zera330-manual-de-identidade.pdf` (fora do repositório). Regras que o código e os documentos seguem:

- Nome sempre em minúsculas: **zera330**. "Neo Geo" aparece só como a plataforma compatível, nunca como nome do produto.
- Cores: grafite `#14161A` (base), amarelo neon `#FFE600` (principal), azul neon `#00C8FF` (só sobre fundo escuro), azul profundo `#06506F`, metal `#9A9CA1`, claro `#F4F5F0`. Sobre fundo claro, texto azul em `#0077A3`.
- Tipografia: Poppins (Bold, Medium, Regular), a mesma do ZeraPS5.
- Logo horizontal: `docs/img/zera330-logo.png` (fundo escuro, versão principal).
- Ícone: "z" em pixels numa grade 7×7, grafite sobre amarelo, bloco do canto em azul, com scanlines (`docs/img/zera330-icon.png`, gerado também no `platform/sdl2/main.c`). Tamanho mínimo 24 px.
- Abertura: o vídeo de 3 s da marca toca antes de cada jogo. Fonte: o MP4 da identidade, extraído com `tools/intro_extract.c` (Media Foundation, só offline) e empacotado com `tools/intro_pack.py` em `assets/zera330_intro.z3i` (640×360, 30 fps, 7 bits por cor, som de moeda).
- Mascote: a capivara do ZeraPS5, com capuz amarelo, fone azul, 330 no moletom e joystick. Sempre sobre fundo escuro.
- Não usar o logo do Neo Geo, o slogan "MAX 330 MEGA PRO-GEAR SPEC", a tipografia original da SNK nem arte de jogos.
