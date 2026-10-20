"""Match game functions against a built libultra archive by masked byte signature.

Usage: tools/match_libultra.py build/recomp-input/image.bin build/recomp-input/syms.toml path/to/libultra_rom.a

Build the archive from https://github.com/decompals/ultralib (VERSION=I matches
this game). Results go to build/libultra-matches.json; recomp/function_names.txt
was produced from them, with a few ambiguous pairs resolved by link order."""
import sys, os, re, subprocess, struct, glob, collections, json
OD='mips-linux-gnu-objdump'; OC='mips-linux-gnu-objcopy'
def lib_funcs(archive, tmp):
    os.makedirs(tmp, exist_ok=True)
    subprocess.run(['ar','x',os.path.abspath(archive)],cwd=tmp,check=True)
    out=[]
    for o in glob.glob(tmp+'/*.o'):
        syms=subprocess.run([OD,'-t',o],capture_output=True,text=True).stdout
        funcs=[]
        for l in syms.splitlines():
            p=l.split()
            if len(p)>=6 and p[-3]=='.text' and ('F' in p[1:4] or p[2]=='F'):
                funcs.append((int(p[0],16),int(p[-2],16),p[-1]))
        if not funcs: continue
        b=o+'.text'; subprocess.run([OC,'-O','binary','-j','.text',o,b],check=True)
        text=open(b,'rb').read()
        rel=subprocess.run([OD,'-r','-j','.text',o],capture_output=True,text=True).stdout
        masks={}; callee={}
        for l in rel.splitlines():
            m=re.match(r'([0-9a-f]{8})\s+(R_MIPS_\w+)\s+(\S+)',l)
            if m:
                masks[int(m.group(1),16)]=m.group(2)
                if m.group(2)=='R_MIPS_26': callee[int(m.group(1),16)]=m.group(3)
        for off,size,name in funcs:
            if size<8: continue
            words=[struct.unpack_from('>I',text,off+i)[0] for i in range(0,size,4)]
            mk=[]
            for i in range(len(words)):
                t=masks.get(off+i*4)
                if t=='R_MIPS_26': mk.append(0xFC000000)
                elif t in ('R_MIPS_HI16','R_MIPS_LO16','R_MIPS_GPREL16'): mk.append(0xFFFF0000)
                else: mk.append(0xFFFFFFFF)
            # trim trailing nops
            while words and words[-1]==0: words.pop(); mk.pop()
            calls=[callee.get(off+i*4) for i in range(len(words))]
            out.append((name,words,mk,calls))
    return out

def game_funcs(img, toml):
    t=open(toml).read(); img=open(img,'rb').read()
    res=[]
    for sec in t.split('[[section]]')[1:]:
        m=re.search(r'rom = 0x([0-9A-F]+)\nvram = 0x([0-9A-F]+)',sec)
        rom,vram=int(m.group(1),16),int(m.group(2),16)
        for n,v,s in re.findall(r'name = "(\S+)", vram = 0x([0-9A-F]+), size = 0x([0-9A-F]+)',sec):
            v,s=int(v,16),int(s,16); r=rom+v-vram
            w=[struct.unpack_from('>I',img,r+i)[0] for i in range(0,s,4)]
            while w and w[-1]==0: w.pop()
            res.append((n,v,w))
    return res

if __name__=='__main__':
    img,toml=sys.argv[1],sys.argv[2]; archives=sys.argv[3:]
    games=game_funcs(img,toml)
    bylen=collections.defaultdict(list)
    for g in games: bylen[len(g[2])].append(g)
    found=collections.defaultdict(set)
    cand_calls={}
    gw_by=dict(((g[0],g[1]),g[2]) for g in games)
    addr_name={}
    for a in archives:
        ver=a.split('/')[-3]
        for name,words,mk,calls in lib_funcs(a,'build/libultra-objs/'+ver):
            if len(words)<3: continue
            for gn,gv,gw in bylen[len(words)]:
                # In the game, jal immediates and hi/lo immediates differ; only compare under the lib mask,
                # and also ignore immediates where the game itself has relocated values (game words may differ in hi/lo of absolute addresses).
                if all((x&m)==(y&m) for x,y,m in zip(words,gw,mk)):
                    found[(gn,gv)].add((name,ver))
                    cand_calls[(gn,gv,name)]=[(i,c) for i,c in enumerate(calls) if c]
    # Resolve ambiguous matches using the names of the functions they call.
    named={gv:next(iter({n for n,_ in s})) for (gn,gv),s in found.items() if len({n for n,_ in s})==1}
    for _ in range(5):
        for (gn,gv),s in found.items():
            names={n for n,_ in s}
            if len(names)<2: continue
            words=gw_by[(gn,gv)]
            ok=[]
            for n in names:
                good=True; checked=0
                for i,c in cand_calls.get((gn,gv,n),[]):
                    t=0x80000000|((words[i]&0x3FFFFFF)<<2) if (gv>>28)==8 else None
                    t=(gv&0xF0000000)|((words[i]&0x3FFFFFF)<<2)
                    if t in named:
                        checked+=1
                        if named[t]!=c: good=False
                if good and checked: ok.append(n)
            if len(ok)==1:
                found[(gn,gv)]={(ok[0],v) for n,v in s if n==ok[0]}
                named[gv]=ok[0]
    out={}
    for (gn,gv),s in sorted(found.items(),key=lambda x:x[0][1]):
        names=sorted({n for n,_ in s}); out[gn]={'vram':gv,'names':names,'versions':sorted({v for _,v in s})}
    json.dump(out,open('build/libultra-matches.json','w'),indent=1)
    uniq=sum(1 for v in out.values() if len(v['names'])==1)
    print(len(out),'matched,',uniq,'unambiguous')
    vc=collections.Counter(v for x in out.values() for v in x['versions']); print(vc)
