#\!/bin/bash
gcc -Wall -g -I../ione-code/bpv7/include -I../ione-code/ici/include dtnexc.c -o dtnexc -L/usr/local/lib -lbp -lici -lm -lpthread -lcrypto
if [ $? -eq 0 ]; then
  echo "Compilation successful\! Running dtnexc..."
  ./dtnexc
else
  echo "Compilation failed"
fi
