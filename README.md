# Nimbus OS 0.1

Sistema operacional feito do zero: bootloader, kernel e interface graficas proprios.
Bootloader, kernel e interface proprios, sem GRUB. O desktop e inspirado no macOS
(barra de menu, dock com ampliacao, botoes coloridos), com arte procedural propria.
A interface usa Noto Sans (SIL OFL 1.1, em `assets/OFL.txt`); o terminal usa a fonte bitmap original.

## O que ja funciona
- Bootloader em 2 estagios (BIOS/MBR): liga A20, carrega o kernel, ativa video VBE 32 bits
  (1280x720, senao 1024x768, senao 800x600) e entra em modo protegido.
- Kernel em C (32 bits): framebuffer, texto, formas com cantos arredondados, transparencia.
- Mouse e teclado PS/2, relogio (RTC).
- Janelas: arrastar, fechar (vermelho), minimizar (amarelo), maximizar (verde), foco.
- Dock com ampliacao e indicador de app aberto; barra de menu com relogio.
- Fonte proporcional suavizada, menus funcionais, nomes no dock e ampliacao animada.
- Busca de aplicativos: **Ctrl+Espaco**, setas e Enter; **Esc** fecha a busca/menu.
- **Alt+Tab** alterna janelas, **Alt+F4** fecha e **Ctrl+M** minimiza.
- Arraste usa cache de janela/fundo e copia somente a regiao afetada; o dock atualiza sua faixa.
- Copias/preenchimentos SSE2 quando a CPU suporta, com fallback escalar.
- Apps: Terminal (help, clear, echo, time, sysinfo, apps, open, reboot), Notas,
  Calculadora, Sobre, **Ajustes** e **Rede**.
- Ajustes: tres papeis de parede, quatro cores de destaque, animacao do dock e metricas de desenho.
- Rede: driver PCI RTL8139, Ethernet, ARP, IPv4, ICMP (ping), UDP e consulta DNS A.

## Compilar (precisa de gcc, binutils, python3)
    ./build.sh            # gera nimbus.img
    qemu-system-i386 -drive file=nimbus.img,format=raw -m 128 -nic user,model=rtl8139

## Rede no QEMU
Abra **Rede** no dock ou na busca. Use **Testar gateway**, ou digite um dominio
no campo e pressione Enter para consultar o DNS. No Terminal: `ping`,
`dns example.com`, `netinfo`, `open net` e `open settings`.

O perfil e estatico: IP `10.0.2.15/24`, gateway `10.0.2.2`, DNS `10.0.2.3`,
compativel com a rede user/NAT padrao do QEMU. O ping testa o gateway virtual;
o DNS consulta nomes externos. Ainda nao ha DHCP, TCP, HTTP/HTTPS, navegador,
Wi-Fi ou drivers para outras placas. Os ajustes visuais tambem ficam em RAM.

## Gravar no pendrive  (APAGA TUDO no pendrive!)
- Windows: Rufus ou balenaEtcher, escolha nimbus.img (no Rufus, modo "Imagem DD").
- Linux:   sudo dd if=nimbus.img of=/dev/sdX bs=1M conv=fsync   (confira o sdX!)

Depois entre na BIOS (F2/F12/Del) e:
1. Desative o **Secure Boot**.
2. Ative o modo **Legacy / CSM**. PCs sem CSM (UEFI puro) NAO bootam esta versao.
3. Escolha o pendrive no menu de boot.

## Limites conhecidos (v0.1)
- So testado no QEMU, ainda nao em hardware real. Se a CPU gerar uma excecao, a tela fica vermelha.
- Mouse/teclado: PS/2 ou USB com "USB Legacy Support" ligado na BIOS. Sem driver USB proprio ainda.
- Teclado em layout US (ABNT2 e o proximo ajuste). Fonte so ASCII, sem acentos.
- Timer PIT a 240 Hz com idle da CPU, mouse a 200 amostras/s e alvo de 60 quadros/s.
- Sem disco, som nem multitarefa. Buffers em enderecos fixos; requer ao menos 32 MB de RAM (QEMU recomendado: 128 MB).
- Sem UEFI, sem resolucao nativa de tela (depende dos modos VBE da BIOS).

## Fonte da interface
`kernel/uifont.h` ja vem gerado. Para regenerar, instale Pillow e execute
`python tools/genfont.py`. A fonte e sua licenca estao em `assets/`.
Notas sao mantidas apenas em RAM; a busca encontra os seis aplicativos integrados.

## Testes integrados (QEMU + Pillow)
    python tools/desktop_smoke.py --output build/test-desktop
    python tools/network_smoke.py --output build/test-network

Use `--qemu caminho/para/qemu-system-i386` e `--nm caminho/para/i686-elf-nm`
quando as ferramentas nao estiverem no PATH. Os testes usam uma VM isolada com
`-snapshot`; a suite de rede salva uma captura PCAP e testa a volta do ring RX,
DNS externo e desconexao/reconexao do link.
