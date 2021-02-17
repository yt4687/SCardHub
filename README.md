すでにアクセスできなくなっているSCardHubのミラーです  
以下、オリジナル  

# Smart Card API Hub for DTV apps. #

DTVアプリ用の特殊なスマートカードリーダとシステム標準のスマートカードリーダを共存させ、  
扱いやすいようにするためのWinSCard.dllハブです。

スマートカードリーダ或いはそれを模したDLLに対応しています。

## 導入 ##

* WinSCard.dll (SCardHub)

	DTVアプリケーションのexeと同じフォルダに配置してください。

* SCardHub.ini

	設定は`SCardHub.ini`に記述します。通常は`WinSCard.dll`と同じフォルダに配置して使います。  
	詳細はサンプルの`SCardHub.ini`に書いてあるコメントを参照してください。

## ビルド環境 ##

以下のコンパイラでビルド可能を確認しています。

* Microsoft Visual C++ 2015 Version 14.0.25425.01 Update 3
* x86_64-w64-mingw32-gcc (GCC) 4.9.2
* i686-w64-mingw32-gcc (GCC) 4.9.2

## ライセンス ##

The MIT License (MIT)

Copyright (c) 2015 jd2015

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

