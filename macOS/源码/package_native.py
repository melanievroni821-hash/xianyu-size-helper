from pathlib import Path
import struct,plistlib,hashlib,json

root=Path(__file__).parent
parts=[(0x01000007,3,root/'lrom-launcher-x86_64'),(0x0100000c,0,root/'lrom-launcher-arm64')]
alignment=16384
offset=alignment
headers=[];payloads=[];metadata=[]
for cpu,sub,path in parts:
    data=path.read_bytes()
    magic,got_cpu,got_sub,filetype,ncmds,szcmds,flags,reserved=struct.unpack_from('<8I',data)
    assert magic==0xfeedfacf and got_cpu==cpu and filetype==2
    cmd_offset=32;main_found=False;codesign_found=False;libraries=[]
    for _ in range(ncmds):
        cmd,size=struct.unpack_from('<II',data,cmd_offset)
        if cmd==0x80000028:main_found=True
        if cmd==0x1d:
            codesign_found=True
            blob_offset,blob_size=struct.unpack_from('<II',data,cmd_offset+8)
            assert blob_offset+blob_size<=len(data)
            # Parse the embedded CodeDirectory and validate every code-page hash.
            blob=data[blob_offset:blob_offset+blob_size]
            blob_magic,total_length,count=struct.unpack_from('>III',blob)
            assert blob_magic==0xfade0cc0 and total_length<=blob_size
            checked=False
            for i in range(count):
                slot,index=struct.unpack_from('>II',blob,12+8*i)
                if slot!=0:continue
                cd=blob[index:]
                fields=struct.unpack_from('>9I',cd)
                assert fields[0]==0xfade0c02
                _,cdlen,version,csflags,hash_offset,ident_offset,special_slots,code_slots,limit=fields
                hash_size,hash_type,platform,page_exponent=struct.unpack_from('4B',cd,36)
                assert hash_type==2 and hash_size==32
                page=1<<page_exponent
                for page_index in range(code_slots):
                    actual=hashlib.sha256(data[page_index*page:min((page_index+1)*page,limit)]).digest()
                    stored=cd[hash_offset+page_index*hash_size:hash_offset+(page_index+1)*hash_size]
                    assert actual==stored,(path,page_index)
                checked=True
            assert checked
        if cmd==0xc:
            name_offset=struct.unpack_from('<I',data,cmd_offset+8)[0]
            library=data[cmd_offset+name_offset:cmd_offset+size].split(b'\0')[0].decode()
            libraries.append(library)
        cmd_offset+=size
    assert main_found and (codesign_found or cpu==0x01000007),(path,'missing native main or required ARM code signature')
    assert libraries==['/usr/lib/libSystem.B.dylib'],libraries
    headers.append(struct.pack('>5I',cpu,sub,offset,len(data),14))
    payloads.append((offset,data))
    metadata.append({'architecture':path.name,'size':len(data),'native_main':True,'embedded_ad_hoc_signature_pages':'verified' if codesign_found else 'Intel slice unsigned; no kernel ad-hoc-signature requirement','library':libraries})
    offset=((offset+len(data)+alignment-1)//alignment)*alignment
output=bytearray(offset)
output[:8]=struct.pack('>II',0xcafebabe,2)
for i,h in enumerate(headers):output[8+20*i:28+20*i]=h
for offset,data in payloads:output[offset:offset+len(data)]=data
destination=root.parent/'咸鱼尺寸助手.app/Contents/MacOS/lrom-launcher'
destination.parent.mkdir(parents=True,exist_ok=True)
destination.write_bytes(output);destination.chmod(0o755)
(root/'native_validation.json').write_text(json.dumps({'fat_binary':'two architectures, 16 KiB alignment','slices':metadata,'macOS_Finder_runtime':'not executed'},indent=2)+'\n')
print(json.dumps({'universal_executable':str(destination),'size':len(output),'slices':metadata},indent=2))
