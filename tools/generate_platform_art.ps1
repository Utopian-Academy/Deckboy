# SPDX-License-Identifier: GPL-3.0-or-later
# Regenerate checked-in platform art on Windows (PowerShell + System.Drawing).
# Packaging on all platforms consumes these assets without an image toolchain.
param([string]$RepoRoot = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Collections.Generic;
public static class DeckboyPlatformArt {
  static string root;
  static Bitmap master;
  static PrivateFontCollection fonts;
  static readonly Color Ink = Color.FromArgb(9,27,12);
  static readonly Color Body = Color.FromArgb(126,151,65);
  static readonly Color Light = Color.FromArgb(218,227,112);
  static readonly Color Mid = Color.FromArgb(73,104,33);
  static readonly Color Screen = Color.FromArgb(39,75,23);
  static readonly Color Play = Color.FromArgb(184,235,30);
  static readonly Color Amber = Color.FromArgb(255,180,24);
  static Bitmap Canvas(int w,int h) { return new Bitmap(w,h,PixelFormat.Format32bppArgb); }
  static void Rect(Bitmap b, Color c, int x,int y,int w,int h) {
    using(var g=Graphics.FromImage(b)) using(var brush=new SolidBrush(c)) g.FillRectangle(brush,x,y,w,h);
  }
  static void Poly(Bitmap b,Color c,params Point[] points) {
    using(var g=Graphics.FromImage(b)) using(var brush=new SolidBrush(c)) g.FillPolygon(brush,points);
  }
  static Bitmap Scale(Bitmap b,int w,int h) {
    var result=Canvas(w,h);
    using(var g=Graphics.FromImage(result)) {
      g.InterpolationMode=InterpolationMode.NearestNeighbor;
      g.PixelOffsetMode=PixelOffsetMode.Half;
      g.DrawImage(b,new Rectangle(0,0,w,h),0,0,b.Width,b.Height,GraphicsUnit.Pixel);
    }
    return result;
  }
  static void Paste(Bitmap b, Bitmap src,int x,int y,int w,int h) {
    using(var scaled=Scale(src,w,h)) using(var g=Graphics.FromImage(b)) g.DrawImageUnscaled(scaled,x,y);
  }
  static void Text(Bitmap b,string text,int cx,int y,int size,Color color) {
    using(var g=Graphics.FromImage(b)) using(var brush=new SolidBrush(color))
    using(var font=new Font(fonts.Families[0],size,FontStyle.Regular,GraphicsUnit.Pixel)) {
      g.TextRenderingHint=TextRenderingHint.SingleBitPerPixelGridFit;
      var fmt=(StringFormat)StringFormat.GenericTypographic.Clone();
      var box=g.MeasureString(text,font,Int32.MaxValue,fmt);
      g.DrawString(text,font,brush,(float)Math.Round(cx-box.Width/2),y,fmt);
      fmt.Dispose();
    }
  }
  static void Save(Bitmap b,string relative) {
    string p=Path.Combine(root,relative); Directory.CreateDirectory(Path.GetDirectoryName(p));
    if(relative.StartsWith("data/icons/")) {
      // BITMAPV4HEADER carries explicit alpha masks for SDL_LoadBMP.
      using(var w=new BinaryWriter(File.Create(p))) {
        int bytes=b.Width*b.Height*4;w.Write((ushort)0x4d42);w.Write(122+bytes);w.Write(0);w.Write(122);
        w.Write(108);w.Write(b.Width);w.Write(b.Height);w.Write((ushort)1);w.Write((ushort)32);w.Write(3);w.Write(bytes);
        w.Write(0);w.Write(0);w.Write(0);w.Write(0);
        w.Write(0x00ff0000);w.Write(0x0000ff00);w.Write(0x000000ff);w.Write(unchecked((int)0xff000000));w.Write(0x73524742);
        w.Write(new byte[48]);
        for(int y=b.Height-1;y>=0;y--)for(int x=0;x<b.Width;x++)w.Write(b.GetPixel(x,y).ToArgb());
      }
    } else b.Save(p,relative.EndsWith(".bmp")?ImageFormat.Bmp:ImageFormat.Png);
  }
  // Authored at the destination grid: a 16 px cartridge keeps its notch,
  // label, screen, amber tally and play triangle without unreadable lettering.
  static Bitmap Small(int n) {
    var b=Canvas(n,n);
    if(n==16) {
      Rect(b,Ink,2,3,13,12);Rect(b,Ink,4,1,9,14);
      Rect(b,Body,3,3,11,11);Rect(b,Body,4,2,8,2);
      Rect(b,Light,4,2,8,1);Rect(b,Light,3,4,1,9);
      Rect(b,Mid,5,3,6,1);Rect(b,Ink,4,5,9,7);Rect(b,Screen,5,6,7,5);
      Rect(b,Play,7,7,1,3);Rect(b,Play,8,8,1,1);
      Rect(b,Amber,11,6,1,1);Rect(b,Ink,10,13,3,1);return b;
    }
    int m=2, u=Math.Max(1,n/16);
    Rect(b,Ink,m,m+2*u,n-2*m,n-2*m-2*u);
    Rect(b,Ink,m+2*u,m,n-2*m-3*u,n-2*m);
    Rect(b,Body,m+u,m+3*u,n-2*m-2*u,n-2*m-4*u);
    Rect(b,Body,m+3*u,m+u,n-2*m-5*u,3*u);
    Rect(b,Light,m+3*u,m+u,n-2*m-5*u,u);
    Rect(b,Light,m+u,m+3*u,u,n-2*m-5*u);
    Rect(b,Mid,m+3*u,m+3*u,n-2*m-6*u,2*u);
    if(n>=32) {
      for(int x=m+4*u;x<n-m-4*u;x+=2*u) Rect(b,Ink,x,m+3*u,u,u);
      Rect(b,Ink,n-m-4*u,m+3*u,2*u,u);
      Rect(b,Ink,n-m-4*u,m+5*u,2*u,u);
    }
    int sx=m+2*u,sy=(int)Math.Round(n*.34),sw=n-2*m-4*u,sh=(int)Math.Round(n*.44);
    Rect(b,Ink,sx,sy,sw,sh); Rect(b,Screen,sx+u,sy+u,sw-2*u,sh-2*u);
    if(n>=32) for(int x=sx+2*u;x<sx+sw-2*u;x+=2*u) Rect(b,Mid,x,sy+sh-2*u,u,u);
    PlayMark(b,sx+u,sy+u,sw-2*u,sh-2*u,Math.Max(1,u/2));
    Rect(b,Amber,sx+sw-2*u,sy+u,u,u);
    Rect(b,Ink,n-m-5*u,n-m-u,3*u,u);
    if(n>=32) for(int x=m+3*u;x<m+6*u;x+=2*u) Rect(b,Ink,x,n-m-u,u,u);
    return b;
  }
  // The master's play mark: an outlined triangle about 60% of the screen's
  // height, centred, with its left edge lit. Rasterised by pixel centre so the
  // rows are symmetric about the point.
  static void PlayMark(Bitmap b,int x,int y,int w,int h,int line) {
    int th=(int)Math.Round(h*.6); if(th%2==0) th--; th=Math.Max(3,th);
    int tw=Math.Max(2,(int)Math.Round(th*.85));
    int x0=x+(w-tw)/2+Math.Max(0,tw/10), y0=y+(h-th)/2; double half=th/2.0;
    var fill=new bool[tw,th];
    for(int r=0;r<th;r++) { double reach=tw*(1-Math.Abs(r+.5-half)/half);
      for(int c=0;c<tw;c++) fill[c,r]=c+.5<=reach; }
    for(int r=-line;r<th+line;r++) for(int c=-line;c<tw+line;c++) {
      bool near=false;
      for(int dr=-line;dr<=line&&!near;dr++) for(int dc=-line;dc<=line&&!near;dc++) {
        int rr=r+dr,cc=c+dc; near=rr>=0&&rr<th&&cc>=0&&cc<tw&&fill[cc,rr]; }
      if(near) b.SetPixel(x0+c,y0+r,Ink); }
    for(int r=0;r<th;r++) for(int c=0;c<tw;c++) if(fill[c,r]) b.SetPixel(x0+c,y0+r,c<line?Light:Play);
  }
  static Bitmap App(int n) { return n<=64?Small(n):Scale(master,n,n); }
  static Bitmap Show(int n) {
    int grid=n<=48?n:48; var b=Canvas(grid,grid); int u=Math.Max(1,grid/16);
    int left=2*u,top=u,right=grid-2*u,bottom=grid-u,fold=4*u;
    Poly(b,Ink,new Point(left,top),new Point(right-fold,top),new Point(right,top+fold),new Point(right,bottom),new Point(left,bottom));
    Poly(b,Light,new Point(left+u,top+u),new Point(right-fold,top+u),new Point(right-u,top+fold),new Point(right-u,bottom-u),new Point(left+u,bottom-u));
    Poly(b,Mid,new Point(right-fold,top),new Point(right-fold,top+fold),new Point(right,top+fold));
    using(var a=Small(grid==16?16:32)) Paste(b,a,left+u,top+4*u,grid-6*u,grid-6*u);
    return n==grid?b:Scale(b,n,n);
  }
  static Bitmap Mac(int n) {
    if(n<=64) {
      var b=Canvas(n,n); int m=Math.Max(1,(int)Math.Round(n*100.0/1024));
      double half=(n-2*m)/2.0;
      for(int y=m;y<n-m;y++)for(int x=m;x<n-m;x++)
        if(Math.Pow(Math.Abs((x+.5-n/2.0)/half),4.5)+Math.Pow(Math.Abs((y+.5-n/2.0)/half),4.5)<=1) b.SetPixel(x,y,Screen);
      using(var a=Small(n)) Paste(b,a,m,m,n-2*m,n-2*m); return b;
    }
    var big=Canvas(1024,1024);
    // macOS legacy grid: 824 px body, 100 px inset, continuous-corner profile.
    for(int y=70;y<974;y++) for(int x=70;x<954;x++) {
      double dx=Math.Abs((x-512.0)/412),dy=Math.Abs((y-512.0)/412);
      double edge=Math.Pow(Math.Pow(dx,4.5)+Math.Pow(dy,4.5),1/4.5);
      double shadow=Math.Pow(Math.Pow(dx,4.5)+Math.Pow(Math.Abs((y-526.0)/412),4.5),1/4.5);
      if(shadow<1.065) big.SetPixel(x,y,Color.FromArgb((int)(42*Math.Max(0,Math.Min(1,(1.065-shadow)/.065))),0,0,0));
      if(edge<=1) big.SetPixel(x,y,Color.FromArgb(255,48+(int)((924-y)*.02),73+(int)((924-y)*.035),27));
    }
    Paste(big,master,130,130,764,764);
    var result=Scale(big,n,n); big.Dispose();return result;
  }
  static void Ico(string path,Func<int,Bitmap> art) {
    int[] sizes={16,24,32,48,64,128,256}; var chunks=new List<byte[]>();
    foreach(int n in sizes) using(var b=art(n)) using(var ms=new MemoryStream()) { b.Save(ms,ImageFormat.Png); chunks.Add(ms.ToArray()); }
    Directory.CreateDirectory(Path.GetDirectoryName(Path.Combine(root,path)));
    using(var w=new BinaryWriter(File.Create(Path.Combine(root,path)))) {
      w.Write((ushort)0);w.Write((ushort)1);w.Write((ushort)sizes.Length); int offset=6+16*sizes.Length;
      for(int i=0;i<sizes.Length;i++) {w.Write((byte)(sizes[i]%256));w.Write((byte)(sizes[i]%256));w.Write((byte)0);w.Write((byte)0);w.Write((ushort)1);w.Write((ushort)32);w.Write(chunks[i].Length);w.Write(offset);offset+=chunks[i].Length;}
      foreach(var data in chunks)w.Write(data);
    }
  }
  static void Svg(Bitmap b,string relative) {
    var s=new StringBuilder();s.Append("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "+b.Width+" "+b.Height+"\" shape-rendering=\"crispEdges\">\n");
    for(int y=0;y<b.Height;y++) for(int x=0;x<b.Width;) {
      Color c=b.GetPixel(x,y);int end=x+1;while(end<b.Width&&b.GetPixel(end,y).ToArgb()==c.ToArgb())end++;
      if(c.A!=0)s.AppendFormat("<path fill=\"#{0:X2}{1:X2}{2:X2}\" d=\"M{3} {4}h{5}v1H{3}z\"/>\n",c.R,c.G,c.B,x,y,end-x);x=end;
    }
    s.Append("</svg>\n");string p=Path.Combine(root,relative);Directory.CreateDirectory(Path.GetDirectoryName(p));File.WriteAllText(p,s.ToString(),new UTF8Encoding(false));
  }
  static Bitmap Wizard(int w,int h) {
    var b=Canvas(w,h); Rect(b,Ink,0,0,w,h);
    double scale=w/164.0;int pad=(int)Math.Round(14*scale), unit=Math.Max(1,(int)Math.Round(scale));
    Rect(b,Mid,pad,(int)(24*scale),w-2*pad,unit);
    Text(b,"DECKBOY",w/2,(int)(43*scale),(int)Math.Max(8,Math.Round(16*scale)),Light);
    int a=w-2*pad;using(var icon=App(256))Paste(b,icon,pad,(int)(88*scale),a,a);
    Text(b,"READY TO",w/2,(int)(251*scale),(int)Math.Max(8,Math.Round(8*scale)),Body);
    Text(b,"PLAY",w/2,(int)(269*scale),(int)Math.Max(8,Math.Round(12*scale)),Play);
    Rect(b,Mid,pad,h-(int)(22*scale),w-2*pad,unit);return b;
  }
  static Bitmap WizardSmall(int n) {
    var b=Canvas(n,n);Rect(b,Ink,0,0,n,n);int pad=Math.Max(2,n/12);using(var a=App(64))Paste(b,a,pad,pad,n-2*pad,n-2*pad);return b;
  }
  static Bitmap Dmg(int scale) {
    var b=Canvas(640*scale,480*scale);Rect(b,Light,0,0,b.Width,b.Height);
    Rect(b,Ink,0,0,b.Width,100*scale);Text(b,"DECKBOY",320*scale,26*scale,32*scale,Light);
    Text(b,"DRAG TO APPLICATIONS",320*scale,122*scale,16*scale,Ink);
    Rect(b,Mid,290*scale,228*scale,48*scale,12*scale);
    Poly(b,Mid,new Point(330*scale,212*scale),new Point(352*scale,234*scale),new Point(330*scale,256*scale));
    Rect(b,Mid,32*scale,336*scale,576*scale,scale);
    Text(b,"READY TO PLAY.",250*scale,402*scale,12*scale,Ink);return b;
  }
  public static void Run(string baseDir) {
    root=Path.GetFullPath(baseDir); master=new Bitmap(Path.Combine(root,"art/windows/icons/deckboy_app_master.png"));
    fonts=new PrivateFontCollection();fonts.AddFontFile(Path.Combine(root,"data/PressStart2P.ttf"));
    int[] sizes={16,24,32,48,64,128,256,512};
    foreach(int n in sizes) {
      using(var a=App(n)) { Save(a,"art/linux/hicolor/"+n+"x"+n+"/apps/io.github.utopian_academy.Deckboy.png");Save(a,"codex-previews/app-"+n+".png");
        if(n==512) Save(a,"tools/flatpak/io.github.utopian_academy.Deckboy.png");
        if(n==32||n==48||n==64||n==128) Save(a,"data/icons/deckboy-"+n+".bmp"); }
      using(var b=Show(n)) {Save(b,"art/linux/hicolor/"+n+"x"+n+"/mimetypes/application-x-deckboy-show.png");Save(b,"codex-previews/show-"+n+".png");}
    }
    using(var a=Small(48)) Svg(a,"art/linux/hicolor/scalable/apps/io.github.utopian_academy.Deckboy.svg");
    using(var a=Show(48)) Svg(a,"art/linux/hicolor/scalable/mimetypes/application-x-deckboy-show.svg");
    Ico("art/windows/icons/deckboy_app.ico",App);Ico("art/windows/icons/deckboy_show.ico",Show);
    int[] macSizes={16,32,64,128,256,512,1024};
    foreach(int n in macSizes) {using(var a=Mac(n)){Save(a,"art/macos/icons/deckboy-"+n+".png");Save(a,"codex-previews/macos-"+n+".png");}
      using(var a=Show(n)){Save(a,"art/macos/icons/show-"+n+".png");Save(a,"codex-previews/macos-show-"+n+".png");}}
    using(var fg=Canvas(1024,1024)){Paste(fg,master,52,52,920,920);Save(fg,"art/macos/Deckboy.icon/Assets/cartridge.png");Save(fg,"codex-previews/macos-composer-layer.png");
      using(var flat=Canvas(1024,1024)){Rect(flat,Screen,0,0,1024,1024);Paste(flat,fg,0,0,1024,1024);Save(flat,"codex-previews/macos-composer-flat.png");}}
    int[,] wizardSizes={{164,314},{202,386},{240,459},{290,556},{315,604},{366,700},{416,797},{269,515},{336,643},{403,772},{430,824},{498,953},{534,1022}};
    for(int i=0;i<wizardSizes.GetLength(0);i++){int w=wizardSizes[i,0],h=wizardSizes[i,1];using(var b=Wizard(w,h)){Save(b,"art/windows/installer/wizard-"+w+".bmp");Save(b,"codex-previews/wizard-"+w+".png");}}
    int[] smallSizes={58,71,85,103,112,129,147,77,97,116,124,143,159};
    foreach(int n in smallSizes)using(var b=WizardSmall(n)){Save(b,"art/windows/installer/small-"+n+".bmp");Save(b,"codex-previews/installer-small-"+n+".png");}
    foreach(int scale in new int[]{1,2})using(var b=Dmg(scale)){Save(b,"art/macos/dmg-background"+(scale==1?"":"@2x")+".png");Save(b,"codex-previews/dmg-background"+(scale==1?"":"@2x")+".png");}
    using(var sheet=Canvas(900,380)) {
      Rect(sheet,Color.FromArgb(240,240,230),0,0,900,190);Rect(sheet,Ink,0,190,900,190);
      int x=24;foreach(int n in new int[]{16,24,32,48,64}) {using(var a=App(n)){Paste(sheet,a,x,30,n,n);Paste(sheet,a,x,220,n,n);Paste(sheet,a,x+70,22,96,96);Paste(sheet,a,x+70,212,96,96);}x+=176;}
      Save(sheet,"codex-previews/small-icons-light-dark.png");
    }
    fonts.Dispose();master.Dispose();
  }
}
'@
[DeckboyPlatformArt]::Run($RepoRoot)
Write-Output 'Generated platform art and codex-previews.'
