with open('/tmp/eliza_internet.bas', 'r', encoding='latin1') as f:
    text = f.read()

lines = text.splitlines()
cleaned_lines = []

for line in lines:
    line = line.strip()
    if not line:
        continue
    
    # Clean up specific known Altair BASIC compressed constructs:
    line = line.replace('FORX=1 TO N1', 'FOR X = 1 TO N1')
    line = line.replace("IFMID$(I$,L,1)=\"'\"THENI$=LEFT$(I$,L-1)+RIGHT$(I$,LEN(I$)-L):GOTO230",
                        "IF MID$(I$,L,1)=\"'\" THEN I$=LEFT$(I$,L-1)+RIGHT$(I$,LEN(I$)-L):GOTO 230")
    line = line.replace('IFL+4<=LEN(I$)THENIFMID$(I$,L,4)="SHUT"THENPRINT"SHUT UP...":END',
                        'IF L+4<=LEN(I$) THEN IF MID$(I$,L,4)="SHUT" THEN PRINT "SHUT UP...":END')
    line = line.replace('IF S>0 THEN360', 'IF S>0 THEN 360')
    line = line.replace('K$THENS=K:T=L:F$=K$', 'K$ THEN S=K:T=L:F$=K$')
    line = line.replace('GOTO390', 'GOTO 390')
    line = line.replace('GOTO570', 'GOTO 570')
    line = line.replace(':FORX=1 TO N1', ':FOR X=1 TO N1')
    line = line.replace(')THEN540', ') THEN 540')
    line = line.replace('" "THENC$=', '" " THEN C$=')
    line = line.replace('FORX=1TOR(K)', 'FOR X=1 TO R(K)')
    line = line.replace('IFR(K)>N(K)', 'IF R(K)>N(K)')
    
    cleaned_lines.append(line)

print(f'Cleaned {len(cleaned_lines)} lines.')
# Write out with \r line endings, NO \x1a
clean_text = '\r'.join(cleaned_lines) + '\r'
clean_bytes = clean_text.encode('latin1')
print(f'Clean bytes length: {len(clean_bytes)}')

with open('/tmp/eliza_clean.bas', 'wb') as f:
    f.write(clean_bytes)
print('Wrote /tmp/eliza_clean.bas successfully.')
