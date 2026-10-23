#!/usr/bin/env python3
"""Print only porting-relevant DT properties; omit chosen bootargs and device identifiers."""
import argparse
import json
from pathlib import Path
import struct
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('dtb', type=Path)
args=ap.parse_args()
data=args.dtb.read_bytes()
magic,total,off_struct,off_strings,off_reserve,version,last,bootid,size_strings,size_struct=struct.unpack_from('>10I',data)
if magic != 0xd00dfeed or total != len(data):
    raise SystemExit('Bad FDT header')
strings=data[off_strings:off_strings+size_strings]
p=off_struct; stack=[]; nodes={}
while p < off_struct+size_struct:
    token=struct.unpack_from('>I',data,p)[0];p+=4
    if token==1:
        end=data.index(0,p);name=data[p:end].decode();p=(end+4)&~3
        stack.append(name);nodes['/'.join(stack) or '/']={}
    elif token==2: stack.pop()
    elif token==3:
        size,offset=struct.unpack_from('>II',data,p);p+=8
        end=strings.index(0,offset);name=strings[offset:end].decode()
        nodes['/'.join(stack) or '/'][name]=data[p:p+size];p=(p+size+3)&~3
    elif token==4: pass
    elif token==9: break
    else: raise SystemExit(f'Bad FDT token {token}')
def values(name,b):
    if not b:return True
    if name in {'model','compatible','method','enable-method','status'}:return b.rstrip(b'\0').decode().split('\0')
    if len(b)%4==0:return [hex(v) for v in struct.unpack('>'+str(len(b)//4)+'I',b)]
    return b.hex()
result={}
keep={'model','compatible','reg','method','capacity-dmips-mhz','enable-method','status','qcom,msm-id','qcom,board-id','interrupts','spi-max-frequency','touchpanel,display-coords','touchpanel,panel-coords','irq-gpio','reset-gpio','goodix,reset-gpio','goodix,irq-gpio','synaptics,irq-gpio','synaptics,reset-gpio','qcom,mdss-dsi-panel-width','qcom,mdss-dsi-panel-height'}
for path,props in nodes.items():
    compat=props.get('compatible',b'')
    if path=='/' or path.startswith('/cpus/cpu@') and path.count('/')==2 or (path=='/memory' or path.startswith('/memory@')) or b'arm,gic-v3\0' in compat or b'arm,psci' in compat or 'splash' in path or b'oplus,tp' in compat or b'synaptics' in compat or b'goodix' in compat or b'focaltech' in compat:
        result[path]={k:values(k,v) for k,v in props.items() if k in keep}
print(json.dumps(result,indent=2))
