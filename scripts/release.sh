#!/bin/sh
# Sürüm kapısını çalıştırır ve yayın komutlarını basar.
# Etiket, push ve noxc publish bu betiğin içinde çalışmaz.
set -eu
cd "$(dirname "$0")/.."

VERSION="$(python3 -c 'import json; print(json.load(open("nox.json"))["version"])')"
case "$VERSION" in
  *.*.*) ;;
  *)
    echo "nox.json sürümü X.Y.Z olmalı: $VERSION" >&2
    exit 1
    ;;
esac
TAG="v$VERSION"
REPO="github.com/mburakmmm/aura"
DESCRIPTION="Nox için deklaratif masaüstü UI framework'ü"

echo "sürüm $VERSION"
./scripts/test.sh

echo
echo "Test geçti. Çalışma ağacı temizken, main dalında şu sırayı kullan:"
echo
echo "git tag -a $TAG -m \"Aura $VERSION\""
echo "git push -u origin main"
echo "git push origin $TAG"
echo "noxc publish $REPO --ref $TAG --description \"$DESCRIPTION\" --tags ui,framework,desktop"
echo
echo "noxc publish kod göndermez. İndeks kaydı admin onayından sonra görünür."
echo "Etiket, GitHub'da bu sürümün commit'ine işaret etmelidir."
