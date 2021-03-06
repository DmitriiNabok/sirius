import json

fin = open("atoms.in", "r")

fout = open("run.x", "w")

atoms = {}

while 1:
    line = fin.readline()
    if not line: break
    if line.find("atom") == 0:
        
        line = fin.readline()
        s1 = line.split()
        zn = int(s1[0])
        line = fin.readline() # symbol and name
        line = line.replace("'", " ")
        s1 = line.split()
        symbol = s1[0]
        name = s1[1]
        line = fin.readline()
        s1 = line.split()
        mass = float(s1[0]) # mass

        fout.write("./atoms1 0 " + symbol + "\n");
        
        atoms[symbol] = {}
        atoms[symbol]["name"] = name
        atoms[symbol]["zn"] = zn
        atoms[symbol]["mass"] = mass
        
        line = fin.readline() # Rmt
        line = fin.readline() # number of states
        s1 = line.split()
        nst = int(s1[0])
        
        levels = []
        for i in range(nst):
            line = fin.readline() # n l k occ
            s1 = line.split()
            n = int(s1[0]);
            l = int(s1[1]);
            k = int(s1[2]);
            occ = int(s1[3]);
            levels.append([n, l, k, occ])
        atoms[symbol]["levels"] = levels
        
        line = fin.readline() # NIST LDA Etot
        line = line.strip()
        if  (line != ""):
            s1 = line.split()
            atoms[symbol]["NIST_LDA_Etot"] = float(s1[0])

fout.close()
fin.close()

fout = open("atoms.json", "w")
json.dump(atoms, fout, indent=2)
fout.close()





