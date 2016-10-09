/******************************************************************************
 * Copyright © 2014-2016 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

// bugs: construct finaltx, src vs dest messages, rotating myind position

// Todo list:
// a) updating latest notarized height based on the notarized tx data
// b) prevent overwriting blocks below notarized height
// c) detection of special transactions to update list of current notaries
// d) award 5% APR for utxo older than a week when they are spent
// e) round robin mining difficulty

#include "iguana777.h"
#include "notaries.h"

#define CHECKSIG 0xac

struct dpow_entry
{
    bits256 prev_hash;
    uint64_t mask;
    int32_t prev_vout,height;
    uint8_t pubkey[33],k,siglen,sig[76];
};

bits256 dpow_getbestblockhash(struct supernet_info *myinfo,struct iguana_info *coin)
{
    char *retstr; bits256 blockhash;
    memset(blockhash.bytes,0,sizeof(blockhash));
    if ( coin->FULLNODE < 0 )
    {
        if ( (retstr= bitcoind_passthru(coin->symbol,coin->chain->serverport,coin->chain->userpass,"getbestblockhash","")) != 0 )
        {
            //printf("%s getbestblockhash.(%s)\n",coin->symbol,retstr);
            if ( is_hexstr(retstr,0) == sizeof(blockhash)*2 )
                decode_hex(blockhash.bytes,sizeof(blockhash),retstr);
            free(retstr);
        }
    }
    else if ( coin->FULLNODE > 0 || coin->VALIDATENODE > 0 )
    {
        blockhash = coin->blocks.hwmchain.RO.hash2;
    }
    else
    {
        
    }
    return(blockhash);
}

cJSON *dpow_getblock(struct supernet_info *myinfo,struct iguana_info *coin,bits256 blockhash)
{
    char buf[128],str[65],*retstr=0; cJSON *json = 0;
    if ( coin->FULLNODE < 0 )
    {
        sprintf(buf,"\"%s\"",bits256_str(str,blockhash));
        retstr = bitcoind_passthru(coin->symbol,coin->chain->serverport,coin->chain->userpass,"getblock",buf);
        //printf("%s getblock.(%s)\n",coin->symbol,retstr);
    }
    else if ( coin->FULLNODE > 0 || coin->VALIDATENODE > 0 )
    {
        retstr = bitcoinrpc_getblock(myinfo,coin,0,0,blockhash,1,0);
    }
    else
    {
        return(0);
    }
    if ( retstr != 0 )
    {
        json = cJSON_Parse(retstr);
        free(retstr);
    }
    return(json);
}

char *dpow_decoderawtransaction(struct supernet_info *myinfo,struct iguana_info *coin,char *rawtx)
{
    char *retstr,*paramstr; cJSON *array;
    if ( coin->FULLNODE < 0 )
    {
        array = cJSON_CreateArray();
        jaddistr(array,rawtx);
        paramstr = jprint(array,1);
        retstr = bitcoind_passthru(coin->symbol,coin->chain->serverport,coin->chain->userpass,"decoderawtransaction",paramstr);
        //printf("%s decoderawtransaction.(%s) <- (%s)\n",coin->symbol,retstr,paramstr);
        free(paramstr);
    }
    else if ( coin->FULLNODE > 0 || coin->VALIDATENODE > 0 )
    {
        retstr = bitcoinrpc_decoderawtransaction(myinfo,coin,0,0,rawtx,1);
    }
    else
    {
        return(0);
    }
    return(retstr);
}

cJSON *dpow_gettransaction(struct supernet_info *myinfo,struct iguana_info *coin,bits256 txid)
{
    char buf[128],str[65],*retstr=0,*rawtx=0; cJSON *json = 0;
    if ( coin->FULLNODE < 0 )
    {
        sprintf(buf,"[\"%s\", 1]",bits256_str(str,txid));
        if ( (rawtx= bitcoind_passthru(coin->symbol,coin->chain->serverport,coin->chain->userpass,"getrawtransaction",buf)) != 0 )
        {
            retstr = dpow_decoderawtransaction(myinfo,coin,rawtx);
            free(rawtx);
        }
    }
    else if ( coin->FULLNODE > 0 || coin->VALIDATENODE > 0 )
    {
        retstr = bitcoinrpc_getrawtransaction(myinfo,coin,0,0,txid,1);
    }
    else
    {
        return(0);
    }
    if ( retstr != 0 )
    {
        json = cJSON_Parse(retstr);
        free(retstr);
    }
    return(json);
}

cJSON *dpow_listunspent(struct supernet_info *myinfo,struct iguana_info *coin,char *coinaddr)
{
    char buf[128],*retstr; cJSON *json = 0;
    if ( coin->FULLNODE < 0 )
    {
        sprintf(buf,"0, 99999999, [\"%s\"]",coinaddr);
        if ( (retstr= bitcoind_passthru(coin->symbol,coin->chain->serverport,coin->chain->userpass,"listunspent",buf)) != 0 )
        {
            json = cJSON_Parse(retstr);
            //printf("%s (%s) listunspent.(%s)\n",coin->symbol,buf,retstr);
            free(retstr);
        } else printf("%s null retstr from (%s)n",coin->symbol,buf);
    }
    else if ( coin->FULLNODE > 0 || coin->VALIDATENODE > 0 )
    {
        json = iguana_listunspents(myinfo,coin,0,1,coin->longestchain,"");
    }
    else
    {
        return(0);
    }
    return(json);
}

char *dpow_signrawtransaction(struct supernet_info *myinfo,struct iguana_info *coin,char *rawtx,cJSON *vins)
{
    cJSON *array,*privkeys,*item; char *wifstr,*str,*paramstr,*retstr; uint8_t script[256]; int32_t i,n,len,hashtype; struct vin_info V; struct iguana_waddress *waddr; struct iguana_waccount *wacct;
    if ( 0 )//coin->FULLNODE < 0 )
    {
        array = cJSON_CreateArray();
        jaddistr(array,rawtx);
        jaddi(array,vins);
        paramstr = jprint(array,1);
        retstr = bitcoind_passthru(coin->symbol,coin->chain->serverport,coin->chain->userpass,"signrawtransaction",paramstr);
        //printf("%s signrawtransaction.(%s) params.(%s)\n",coin->symbol,retstr,paramstr);
        free(paramstr);
        return(retstr);
    }
    else if ( 1 )//coin->FULLNODE > 0 || coin->VALIDATENODE > 0 )
    {
        privkeys = cJSON_CreateArray();
        if ( (n= cJSON_GetArraySize(vins)) > 0 )
        {
            for (i=0; i<n; i++)
            {
                wifstr = "";
                item = jitem(vins,i);
                if ( (str= jstr(item,"scriptPubkey")) != 0 && is_hexstr(str,0) > 0 && strlen(str) < sizeof(script)*2 )
                {
                    len = (int32_t)strlen(str) >> 1;
                    decode_hex(script,len,str);
                    V.spendlen = len;
                    memcpy(V.spendscript,script,len);
                    if ( (hashtype= _iguana_calcrmd160(coin,&V)) >= 0 && V.coinaddr[0] != 0 )
                    {
                        if ( (waddr= iguana_waddresssearch(myinfo,&wacct,V.coinaddr)) != 0 )
                        {
                            if ( bits256_nonz(waddr->privkey) != 0 )
                            {
                                if ( bitcoin_priv2wif(waddr->wifstr,waddr->privkey,coin->chain->wiftype) > 0 )
                                {
                                    wifstr = waddr->wifstr;
                                }
                            }
                        }
                    }
                }
                jaddistr(privkeys,wifstr);
            }
        }
        retstr = bitcoinrpc_signrawtransaction(myinfo,coin,0,0,rawtx,vins,privkeys,"ALL");
        free_json(privkeys);
        return(retstr);
    }
    else
    {
        return(0);
    }
}

char *dpow_sendrawtransaction(struct supernet_info *myinfo,struct iguana_info *coin,char *signedtx)
{
    bits256 txid; cJSON *json,*array; char *paramstr,*retstr;
    if ( coin->FULLNODE < 0 )
    {
        array = cJSON_CreateArray();
        jaddistr(array,signedtx);
        paramstr = jprint(array,1);
        //printf("%s sendrawtransaction.(%s)\n",coin->symbol,paramstr);
        retstr = bitcoind_passthru(coin->symbol,coin->chain->serverport,coin->chain->userpass,"sendrawtransaction",paramstr);
        free(paramstr);
        return(retstr);
    }
    else if ( coin->FULLNODE > 0 || coin->VALIDATENODE > 0 )
    {
        txid = iguana_sendrawtransaction(myinfo,coin,signedtx);
        json = cJSON_CreateObject();
        jaddbits256(json,"result",txid);
        return(jprint(json,1));
    }
    else
    {
        return(0);
    }
}

int32_t dpow_getchaintip(struct supernet_info *myinfo,bits256 *blockhashp,uint32_t *blocktimep,bits256 *txs,uint32_t *numtxp,struct iguana_info *coin)
{
    int32_t n,i,height = -1,maxtx = *numtxp; bits256 besthash; cJSON *array,*json;
    *numtxp = *blocktimep = 0;
    *blockhashp = besthash = dpow_getbestblockhash(myinfo,coin);
    if ( bits256_nonz(besthash) != 0 )
    {
        if ( (json= dpow_getblock(myinfo,coin,besthash)) != 0 )
        {
            if ( (height= juint(json,"height")) != 0 && (*blocktimep= juint(json,"time")) != 0 )
            {
                if ( (array= jarray(&n,json,"tx")) != 0 )
                {
                    for (i=0; i<n&&i<maxtx; i++)
                        txs[i] = jbits256i(array,i);
                    //printf("dpow_getchaintip %s ht.%d time.%u numtx.%d\n",coin->symbol,height,*blocktimep,n);
                    *numtxp = n;
                }
            } else height = -1;
            free_json(json);
        }
    }
    return(height);
}

int32_t dpow_vini_ismine(struct supernet_info *myinfo,cJSON *item)
{
    cJSON *sobj; char *hexstr; int32_t len; uint8_t data[35];
    if ( (sobj= jobj(item,"scriptPubKey")) != 0 && (hexstr= jstr(sobj,"hex")) != 0 )
    {
        len = (int32_t)strlen(hexstr) >> 1;
        if ( len <= sizeof(data) )
        {
            decode_hex(data,len,hexstr);
            if ( len == 35 && data[34] == CHECKSIG && data[0] == 33 && memcmp(data+1,myinfo->DPOW.minerkey33,33) == 0 )
                return(0);
        }
    }
    return(-1);
}

int32_t dpow_haveutxo(struct supernet_info *myinfo,struct iguana_info *coin,bits256 *txidp,int32_t *voutp,char *coinaddr)
{
    int32_t i,n,vout,haveutxo = 0; bits256 txid; cJSON *unspents,*item; uint64_t satoshis; char *str,*address; uint8_t script[35];
    memset(txidp,0,sizeof(*txidp));
    *voutp = -1;
    if ( (unspents= dpow_listunspent(myinfo,coin,coinaddr)) != 0 )
    {
        if ( (n= cJSON_GetArraySize(unspents)) > 0 )
        {
            /*{
             "txid" : "34bc21b40d6baf38e2db5be5353dd0bcc9fe416485a2a68753541ed2f9c194b1",
             "vout" : 0,
             "address" : "RFBmvBaRybj9io1UpgWM4pzgufc3E4yza7",
             "scriptPubKey" : "21039a3f7373ae91588b9edd76a9088b2871f62f3438d172b9f18e0581f64887404aac",
             "amount" : 3.00000000,
             "confirmations" : 4282,
             "spendable" : true
             },*/
            for (i=0; i<n; i++)
            {
                item = jitem(unspents,i);
                satoshis = SATOSHIDEN * jdouble(item,"amount");
                if ( satoshis == DPOW_UTXOSIZE && (address= jstr(item,"address")) != 0 && strcmp(address,coinaddr) == 0 )
                {
                    if ( (str= jstr(item,"scriptPubKey")) != 0 && is_hexstr(str,0) == sizeof(script)*2 )
                    {
                        txid = jbits256(item,"txid");
                        vout = jint(item,"vout");
                        if ( bits256_nonz(txid) != 0 && vout >= 0 )
                        {
                            if ( *voutp < 0 )
                            {
                                *voutp = vout;
                                *txidp = txid;
                            }
                            haveutxo++;
                        }
                    }
                }
            }
        } // else printf("null array size\n");
        free_json(unspents);
    } else printf("null return from dpow_listunspent\n");
    if ( haveutxo > 0 )
        printf("%s haveutxo.%d\n",coin->symbol,haveutxo);
    return(haveutxo);
}

int32_t dpow_message_utxo(bits256 *hashmsgp,bits256 *txidp,int32_t *voutp,cJSON *json)
{
    cJSON *msgobj,*item; uint8_t key[BASILISK_KEYSIZE],data[sizeof(bits256)*2+1]; char *keystr,*hexstr,str[65],str2[65]; int32_t i,j,n,datalen,retval = -1;
    *voutp = -1;
    memset(txidp,0,sizeof(*txidp));
    if ( (msgobj= jarray(&n,json,"messages")) != 0 )
    {
        //printf("messages.(%s)\n",jprint(msgobj,0));
        for (i=0; i<n; i++)
        {
            item = jitem(msgobj,i);
            if ( (keystr= jstr(item,"key")) != 0 && is_hexstr(keystr,0) == BASILISK_KEYSIZE*2 && (hexstr= jstr(item,"data")) != 0 && (datalen= is_hexstr(hexstr,0)) > 0 )
            {
                decode_hex(key,BASILISK_KEYSIZE,keystr);
                datalen >>= 1;
                if ( datalen <= sizeof(data) )
                {
                    decode_hex(data,datalen,hexstr);
                    if ( datalen == sizeof(data) )
                    {
                        for (j=0; j<sizeof(bits256); j++)
                        {
                            hashmsgp->bytes[j] = data[j];
                            txidp->bytes[j] = data[j + sizeof(bits256)];
                        }
                        *voutp = data[sizeof(bits256) * 2];
                        retval = datalen;
                        printf("notary.%d hashmsg.(%s) txid.(%s) v%d\n",i,bits256_str(str,*hashmsgp),bits256_str(str2,*txidp),*voutp);
                    }
                } else printf("datalen.%d >= maxlen.%d\n",datalen,(int32_t)sizeof(data));
            }
        }
    }
    return(retval);
}

int32_t dpow_message_most(uint8_t *k_masks,int32_t num,cJSON *json,int32_t lastflag)
{
    cJSON *msgobj,*item; uint8_t key[BASILISK_KEYSIZE],data[128]; char *keystr,*hexstr; int32_t duplicate,i,j,n,datalen,most = 0;
    if ( (msgobj= jarray(&n,json,"messages")) != 0 )
    {
        for (i=0; i<n; i++)
        {
            item = jitem(msgobj,i);
            if ( (keystr= jstr(item,"key")) != 0 && is_hexstr(keystr,0) == BASILISK_KEYSIZE*2 && (hexstr= jstr(item,"data")) != 0 && (datalen= is_hexstr(hexstr,0)) > 0 )
            {
                decode_hex(key,BASILISK_KEYSIZE,keystr);
                datalen >>= 1;
                if ( datalen < sizeof(data) )
                {
                    decode_hex(data,datalen,hexstr);
                    for (j=duplicate=0; j<num; j++)
                    {
                        if ( memcmp(data+1,&k_masks[(j << 7) + 1],9) == 0 )
                        {
                            if ( data[0] == k_masks[j << 7] )
                                duplicate++;
                            else
                            {
                                if ( ++k_masks[(j << 7) + 127] == 0 )
                                    k_masks[(j << 7) + 126]++;
                            }
                        }
                    }
                    if ( duplicate == 0 && num < 4096 )
                    {
                        uint8_t *sig; int32_t senderind,lastk,siglen; uint64_t mask;
                        senderind = data[0];
                        lastk = data[1];
                        iguana_rwnum(0,&data[2],sizeof(mask),(uint8_t *)&mask);
                        siglen = data[10];
                        sig = data+11;
                        memcpy(&k_masks[num << 7],data,datalen);
                        //printf("num.%d sender.%d lastk.%d %llx\n",num,senderind,lastk,(long long)mask);
                        num++;
                    }
                } else printf("datalen.%d >= maxlen.%d\n",datalen,(int32_t)sizeof(data));
            }
        }
    }
    if ( lastflag != 0 && num > 0 )
    {
        for (j=0; j<num; j++)
        {
            n = k_masks[(j << 7) + 127] + ((int32_t)k_masks[(j << 7) + 126] << 8);
            if ( n > most )
            {
                most = n;
                memcpy(&k_masks[num << 7],&k_masks[i << 7],1 << 7);
            }
        }
    }
    //printf("most.%d n.%d\n",most,n);
    return(num);
}

int32_t dpow_opreturnscript(uint8_t *script,uint8_t *opret,int32_t opretlen)
{
    int32_t offset = 0;
    script[offset++] = 0x6a;
    if ( opretlen >= 0x4c )
    {
        if ( opretlen > 0xff )
        {
            script[offset++] = 0x4d;
            script[offset++] = opretlen & 0xff;
            script[offset++] = (opretlen >> 8) & 0xff;
        }
        else
        {
            script[offset++] = 0x4c;
            script[offset++] = opretlen;
        }
    } else script[offset++] = opretlen;
    memcpy(&script[offset],opret,opretlen);
    return(opretlen + offset);
}

int32_t dpow_rwopret(int32_t rwflag,uint8_t *opret,bits256 *hashmsg,int32_t *heightmsgp,bits256 *btctxid,char *src)
{
    int32_t i,opretlen = 0;
    opretlen += iguana_rwbignum(rwflag,&opret[opretlen],sizeof(*hashmsg),hashmsg->bytes);
    opretlen += iguana_rwnum(rwflag,&opret[opretlen],sizeof(*heightmsgp),(uint32_t *)heightmsgp);
    if ( bits256_nonz(*btctxid) != 0 )
    {
        opretlen += iguana_rwbignum(rwflag,&opret[opretlen],sizeof(*btctxid),btctxid->bytes);
        if ( rwflag != 0 )
        {
            for (i=0; src[i]!=0; i++)
                opret[opretlen++] = src[i];
            opret[opretlen++] = 0;
        }
        else
        {
            for (i=0; src[i]!=0; i++)
                src[i] = opret[opretlen++];
            src[i] = 0;
            opretlen++;
        }
    }
    return(opretlen);
}

bits256 dpow_notarytx(char *signedtx,int32_t isPoS,uint32_t timestamp,int32_t height,struct dpow_entry notaries[DPOW_MAXRELAYS],int32_t numnotaries,uint64_t mask,int32_t k,bits256 hashmsg,int32_t heightmsg,bits256 btctxid,char *src)
{
    uint32_t i,j,m,locktime,numvouts,version,opretlen,siglen,len,sequenceid = 0xffffffff;
    uint64_t satoshis,satoshisB; uint8_t serialized[16384],opret[256],data[256];
    len = locktime = 0;
    version = 1;
    len += iguana_rwnum(1,&serialized[len],sizeof(version),&version);
    if ( isPoS != 0 )
        len += iguana_rwnum(1,&serialized[len],sizeof(timestamp),&timestamp);
    m = (numnotaries >> 1) + 1;
    len += iguana_rwvarint32(1,&serialized[len],(uint32_t *)&m);
    for (j=m=0; j<numnotaries; j++)
    {
        i = ((height % numnotaries) + j) % numnotaries;
        if ( ((1LL << i) & mask) != 0 )
        {
            len += iguana_rwbignum(1,&serialized[len],sizeof(notaries[i].prev_hash),notaries[i].prev_hash.bytes);
            len += iguana_rwnum(1,&serialized[len],sizeof(notaries[i].prev_vout),&notaries[i].prev_vout);
            siglen = notaries[i].siglen;
            len += iguana_rwvarint32(1,&serialized[len],&siglen);
            if ( siglen > 0 )
                memcpy(&serialized[len],notaries[i].sig,siglen), len += siglen;
            len += iguana_rwnum(1,&serialized[len],sizeof(sequenceid),&sequenceid);
            m++;
            if ( m == numnotaries/2+1 && i == k )
                break;
        }
    }
    numvouts = 2;
    len += iguana_rwvarint32(1,&serialized[len],&numvouts);
    satoshis = DPOW_UTXOSIZE * m * .76;
    if ( (satoshisB= DPOW_UTXOSIZE * m - 10000) < satoshis )
        satoshis = satoshisB;
    len += iguana_rwnum(1,&serialized[len],sizeof(satoshis),&satoshis);
    serialized[len++] = 35;
    serialized[len++] = 33;
    decode_hex(&serialized[len],33,CRYPTO777_PUBSECPSTR), len += 33;
    serialized[len++] = CHECKSIG;
    satoshis = 0;
    len += iguana_rwnum(1,&serialized[len],sizeof(satoshis),&satoshis);
    opretlen = dpow_rwopret(1,opret,&hashmsg,&heightmsg,&btctxid,src);
    opretlen = dpow_opreturnscript(data,opret,opretlen), len += opretlen;
    if ( opretlen < 0xfd )
        serialized[len++] = opretlen;
    else
    {
        serialized[len++] = 0xfd;
        serialized[len++] = opretlen & 0xff;
        serialized[len++] = (opretlen >> 8) & 0xff;
    }
    memcpy(&serialized[len],data,opretlen), len += opretlen;
    len += iguana_rwnum(1,&serialized[len],sizeof(locktime),&locktime);
    init_hexbytes_noT(signedtx,serialized,len);
    return(bits256_doublesha256(0,serialized,len));
}

cJSON *dpow_createtx(struct iguana_info *coin,cJSON **vinsp,struct dpow_entry notaries[DPOW_MAXRELAYS],int32_t numnotaries,int32_t height,int32_t lastk,uint64_t mask,int32_t usesigs,bits256 hashmsg,bits256 btctxid,uint32_t timestamp)
{
    int32_t i,j,m=0,siglen; char scriptstr[256]; cJSON *txobj=0,*vins=0,*item; uint64_t satoshis; uint8_t script[35],*sig;
    if ( (txobj= bitcoin_txcreate(coin->chain->isPoS,0,1,0)) != 0 )
    {
        jaddnum(txobj,"suppress",1);
        jaddnum(txobj,"timestamp",timestamp);
        vins = cJSON_CreateArray();
        for (j=0; j<numnotaries; j++)
        {
            i = ((height % numnotaries) + j) % numnotaries;
            if ( ((1LL << i) & mask) != 0 )
            {
                item = cJSON_CreateObject();
                jaddbits256(item,"txid",notaries[i].prev_hash);
                jaddnum(item,"vout",notaries[i].prev_vout);
                script[0] = 33;
                memcpy(script+1,notaries[i].pubkey,33);
                script[34] = CHECKSIG;
                init_hexbytes_noT(scriptstr,script,35);
                jaddstr(item,"scriptPubKey",scriptstr);
                sig = 0, siglen = 0;
                if ( usesigs != 0 && notaries[i].siglen > 0 )
                {
                    init_hexbytes_noT(scriptstr,notaries[i].sig,notaries[i].siglen);
                    jaddstr(item,"scriptSig",scriptstr);
                    printf("sig%d.(%s)\n",i,scriptstr);
                    sig = notaries[i].sig;
                    siglen = notaries[i].siglen;
                }
                jaddi(vins,item);
                bitcoin_txinput(coin,txobj,notaries[i].prev_hash,notaries[i].prev_vout,0xffffffff,script,sizeof(script),0,0,0,0,sig,siglen);
                m++;
                if ( m == numnotaries/2+1 && i == lastk )
                    break;
            }
        }
        satoshis = DPOW_UTXOSIZE * m * .76;
        script[0] = 33;
        decode_hex(script+1,33,CRYPTO777_PUBSECPSTR);
        script[34] = CHECKSIG;
        txobj = bitcoin_txoutput(txobj,script,sizeof(script),satoshis);
    }
    *vinsp = vins;
    if ( 0 && usesigs != 0 )
        printf("%s createtx.(%s)\n",coin->symbol,jprint(txobj,0));
    return(txobj);
}
    
int32_t dpow_signedtxgen(struct supernet_info *myinfo,struct dpow_info *dp,struct iguana_info *coin,bits256 *signedtxidp,char *signedtx,uint64_t mask,int32_t lastk,struct dpow_entry notaries[DPOW_MAXRELAYS],int32_t numnotaries,int32_t height,int32_t myind,bits256 hashmsg,bits256 btctxid,uint32_t timestamp)
{
    int32_t i,j,siglen,m=0,incr,retval=-1; char rawtx[16384],*jsonstr,*rawtx2,*sigstr; cJSON *txobj,*signobj,*sobj,*txobj2,*vins,*item,*vin; uint8_t data[128]; bits256 txid,srchash,desthash; uint32_t channel;
    incr = sqrt(numnotaries) + 1;
    if ( numnotaries < 8 )
        incr = 1;
    channel = 's' | ('i' << 8) | ('g' << 16) | ('s' << 24);
    if ( bits256_nonz(btctxid) == 0 )
        channel = ~channel;
    for (j=0; j<sizeof(srchash); j++)
        srchash.bytes[j] = myinfo->DPOW.minerkey33[j+1];
    if ( (txobj= dpow_createtx(coin,&vins,notaries,numnotaries,height,lastk,mask,1,hashmsg,btctxid,timestamp)) != 0 )
    {
        txid = dpow_notarytx(rawtx,coin->chain->isPoS,timestamp,height,notaries,numnotaries,mask,lastk,hashmsg,height,btctxid,dp->symbol);
        if ( rawtx[0] != 0 )
        {
            if ( (jsonstr= dpow_signrawtransaction(myinfo,coin,rawtx,vins)) != 0 )
            {
                if ( (signobj= cJSON_Parse(jsonstr)) != 0 )
                {
                    if ( (signedtx= jstr(signobj,"result")) != 0 && (rawtx2= dpow_decoderawtransaction(myinfo,coin,signedtx)) != 0 )
                    {
                        if ( (txobj2= cJSON_Parse(rawtx2)) != 0 )
                        {
                            if ( (vin= jarray(&m,txobj2,"vin")) != 0 )
                            {
                                for (j=0; j<m; j++)
                                {
                                    item = jitem(vin,j);
                                    if ( dpow_vini_ismine(myinfo,item) < 0 )
                                        continue;
                                    printf("myvin.(%s)\n",jprint(item,0));
                                    if ( (sobj= jobj(item,"scriptSig")) != 0 && (sigstr= jstr(sobj,"hex")) != 0 )
                                    {
                                        siglen = (int32_t)strlen(sigstr) >> 1;
                                        data[0] = myind;
                                        data[1] = lastk;
                                        iguana_rwnum(1,&data[2],sizeof(mask),(uint8_t *)&mask);
                                        data[10] = siglen;
                                        decode_hex(data+11,siglen,sigstr);
                                        for (i=(myind % incr); i<numnotaries; i+=incr)
                                        {
                                            for (j=0; j<sizeof(desthash); j++)
                                                desthash.bytes[j] = notaries[i].pubkey[j+1];
                                            //printf("send to notary.%d\n",i);
                                            basilisk_channelsend(myinfo,srchash,desthash,channel,height,data,siglen+11,120);
                                        }
                                        retval = 0;
                                    }
                                }
                            }
                            free_json(txobj2);
                        }
                        free(rawtx2);
                    }
                    free_json(signobj);
                }
                free(jsonstr);
            }
        }
        free_json(txobj);
        //fprintf(stderr,"free vins\n");
        //free_json(vins);
    }
    return(retval);
}

int32_t dpow_k_masks_match(struct dpow_entry notaries[DPOW_MAXRELAYS],int32_t numnotaries,uint8_t *k_masks,int32_t num,int32_t refk,uint64_t refmask,int32_t refheight)
{
    int32_t i,senderind,lastk,matches = 0; uint8_t data[128]; uint64_t mask;
    for (i=0; i<num; i++)
    {
        memcpy(data,&k_masks[i << 7],sizeof(data));
        senderind = data[0];
        lastk = data[1];
        iguana_rwnum(0,&data[2],sizeof(mask),(uint8_t *)&mask);
        if ( senderind < numnotaries && lastk == refk && mask == refmask && notaries[senderind].height == refheight )
        {
            if ( (notaries[senderind].siglen= data[10]) < sizeof(notaries[senderind].sig) )
            {
                notaries[senderind].k = refk;
                notaries[senderind].mask = refmask;
                memcpy(notaries[senderind].sig,data+11,data[10]);
                int32_t j; for (j=0; j<notaries[senderind].siglen; j++)
                    printf("%02x",notaries[senderind].sig[j]);
                if ( notaries[senderind].siglen > 0 )
                    printf(" <- sender.%d siglen.%d\n",i,data[10]);
                matches++;
            }
        }
    }
    printf("matches.%d num.%d k.%d %llx refht.%d\n",matches,num,refk,(long long)refmask,refheight);
    return(matches);
}

int32_t dpow_mostsignedtx(struct supernet_info *myinfo,struct dpow_info *dp,struct iguana_info *coin,bits256 *signedtxidp,char *signedtx,uint64_t *maskp,int32_t *lastkp,struct dpow_entry notaries[DPOW_MAXRELAYS],int32_t numnotaries,int32_t height,int32_t myind,bits256 hashmsg,bits256 btctxid,uint32_t timestamp)
{
    uint32_t channel; uint8_t *k_masks; bits256 srchash,desthash; cJSON *retarray,*item; int32_t i,num,j,k,m,most = 0; uint64_t mask;
    *lastkp = -1;
    *maskp = 0;
    memset(signedtxidp,0,sizeof(*signedtxidp));
    signedtx[0] = 0;
    channel = 's' | ('i' << 8) | ('g' << 16) | ('s' << 24);
    if ( bits256_nonz(btctxid) == 0 )
        channel = ~channel;
    for (j=0; j<sizeof(desthash); j++)
        desthash.bytes[j] = myinfo->DPOW.minerkey33[j+1];
    num = 0;
    k_masks = calloc(4096,128);
    for (i=0; i<numnotaries; i++)
    {
        for (j=0; j<sizeof(srchash); j++)
            srchash.bytes[j] = notaries[i].pubkey[j+1];
        if ( (retarray= basilisk_channelget(myinfo,srchash,desthash,channel,height,0)) != 0 )
        {
            if ( (m= cJSON_GetArraySize(retarray)) != 0 )
            {
                for (k=0; k<m; k++)
                {
                    item = jitem(retarray,k);
                    if ( (num= dpow_message_most(k_masks,num,item,k==m-1)) < 0 )
                        break;
                }
            }
            free_json(retarray);
        }
    }
    if ( num > 0 )
    {
        k = k_masks[(num << 7) + 1];
        iguana_rwnum(0,&k_masks[(num << 7) + 2],sizeof(mask),(uint8_t *)&mask);
        *lastkp = k;
        *maskp = mask;
        if ( (most= dpow_k_masks_match(notaries,numnotaries,k_masks,num,k,mask,height)) >= numnotaries/2+1 )
        {
            char str[65];
            *signedtxidp = dpow_notarytx(signedtx,coin->chain->isPoS,timestamp,height,notaries,numnotaries,mask,k,hashmsg,height,btctxid,dp->symbol);
            printf("notarytx %s %s\n",bits256_str(str,*signedtxidp),signedtx);
        }
    }
    free(k_masks);
    return(most);
}

void dpow_txidupdate(struct supernet_info *myinfo,struct dpow_info *dp,struct iguana_info *coin,uint64_t *recvmaskp,uint32_t channel,int32_t height,struct dpow_entry notaries[DPOW_MAXRELAYS],int32_t numnotaries,int32_t myind,bits256 hashmsg)
{
    int32_t i,j,k,m; cJSON *item,*retarray; bits256 desthash,srchash,checkmsg;
    for (j=0; j<sizeof(srchash); j++)
        srchash.bytes[j] = myinfo->DPOW.minerkey33[j+1];
    for (i=0; i<numnotaries; i++)
    {
        if ( (*recvmaskp & (1LL << i)) != 0 )
            continue;
        for (j=0; j<sizeof(desthash); j++)
            desthash.bytes[j] = notaries[i].pubkey[j+1];
        if ( (retarray= basilisk_channelget(myinfo,desthash,srchash,channel,height,0)) != 0 )
        {
            if ( (m= cJSON_GetArraySize(retarray)) != 0 )
            {
                for (k=0; k<m; k++)
                {
                    item = jitem(retarray,k);
                    if ( dpow_message_utxo(&checkmsg,&notaries[i].prev_hash,&notaries[i].prev_vout,item) == sizeof(bits256)*2+1 )
                    {
                        if ( bits256_cmp(checkmsg,hashmsg) == 0 )
                        {
                            notaries[i].height = height;
                            *recvmaskp |= (1LL << i);
                            break;
                        }
                    }
                }
            }
            free_json(retarray);
        }
    }
}

uint32_t dpow_statemachineiterate(struct supernet_info *myinfo,struct dpow_info *dp,struct iguana_info *coin,uint32_t state,bits256 hashmsg,int32_t heightmsg,bits256 btctxid,struct dpow_entry notaries[DPOW_MAXRELAYS],int32_t numnotaries,int32_t myind,uint64_t *recvmaskp,bits256 *signedtxidp,char *signedtx,uint32_t timestamp)
{
    // todo: add RBF support
    bits256 txid,signedtxid; int32_t vout,completed,i,j,k,m,incr,haveutxo = 0; cJSON *addresses; char *sendtx,*rawtx,*retstr,coinaddr[64]; uint8_t data[sizeof(bits256)*2+1]; uint32_t channel; bits256 srchash,desthash; uint64_t mask;
    if ( numnotaries > 8 )
        incr = sqrt(numnotaries) + 1;
    else incr = 1;
    channel = 'd' | ('P' << 8) | ('o' << 16) | ('W' << 24);
    if ( bits256_nonz(btctxid) == 0 )
        channel = ~channel;
    bitcoin_address(coinaddr,coin->chain->pubtype,myinfo->DPOW.minerkey33,33);
    if ( bits256_nonz(hashmsg) == 0 )
        return(0xffffffff);
    for (j=0; j<sizeof(srchash); j++)
        srchash.bytes[j] = myinfo->DPOW.minerkey33[j+1];
    printf("%s statemachine state.%d %s BTC.%d\n",coin->symbol,state,coinaddr,bits256_nonz(btctxid)==0);
    switch ( state )
    {
        case 0:
            if ( (haveutxo= dpow_haveutxo(myinfo,coin,&txid,&vout,coinaddr)) != 0 )
                state = 1;
            if ( haveutxo < 10 && time(NULL) > dp->lastsplit+600 )
            {
                printf("haveutxo.%d\n",haveutxo);
                addresses = cJSON_CreateArray();
                jaddistr(addresses,coinaddr);
                if ( (rawtx= iguana_utxoduplicates(myinfo,coin,myinfo->DPOW.minerkey33,DPOW_UTXOSIZE,10,&completed,&signedtxid,0,addresses)) != 0 )
                {
                    if ( (sendtx= dpow_sendrawtransaction(myinfo,coin,rawtx)) != 0 )
                    {
                        printf("sendrawtransaction.(%s)\n",sendtx);
                        free(sendtx);
                    }
                    free(rawtx);
                }
                free_json(addresses);
                dp->lastsplit = (uint32_t)time(NULL);
            }
            break;
        case 1: // wait for utxo, send utxo to all other nodes
            if ( (haveutxo= dpow_haveutxo(myinfo,coin,&txid,&vout,coinaddr)) != 0 && vout >= 0 && vout < 0x100 )
            {
                for (i=(myind % incr); i<numnotaries; i+=incr)
                {
                    for (j=0; j<sizeof(srchash); j++)
                    {
                        desthash.bytes[j] = notaries[i].pubkey[j+1];
                        data[j] = hashmsg.bytes[j];
                        data[j+sizeof(bits256)] = txid.bytes[j];
                    }
                    data[sizeof(bits256)*2] = vout;
                    //printf("STATE1: %s send %s %s/v%d\n",coin->symbol,bits256_str(str,hashmsg),bits256_str(str2,txid),vout);
                    basilisk_channelsend(myinfo,srchash,desthash,channel,heightmsg,data,sizeof(data),120);
                }
                state = 2;
            }
            break;
        case 2:
            dpow_txidupdate(myinfo,dp,coin,recvmaskp,channel,heightmsg,notaries,numnotaries,myind,hashmsg);
            printf("STATE2: RECVMASK.%llx\n",(long long)*recvmaskp);
            if ( bitweight(*recvmaskp) > numnotaries/2 )
                state = 3;
            break;
        case 3: // create rawtx, sign, send rawtx + sig to all other nodes
            dpow_txidupdate(myinfo,dp,coin,recvmaskp,channel,heightmsg,notaries,numnotaries,myind,hashmsg);
            k = 0;
            printf("STATE3: RECVMASK.%llx\n",(long long)*recvmaskp);
            if ( bitweight(*recvmaskp) > numnotaries/2+1 )
            {
                printf("too many entries, prune to %d\n",numnotaries/2+1);
                mask = 0;
                for (j=m=0; j<numnotaries; j++)
                {
                    k = ((heightmsg % numnotaries) + j) % numnotaries;
                    if ( ((1LL << k) & *recvmaskp) != 0 )
                    {
                        mask |= (1LL << k);
                        if ( ++m >= numnotaries/2+1 )
                            break;
                    }
                }
            } else mask = *recvmaskp;
            if ( bitweight(mask) == numnotaries/2+1 )
            {
                if ( dpow_signedtxgen(myinfo,dp,coin,signedtxidp,signedtx,mask,k,notaries,numnotaries,heightmsg,myind,hashmsg,btctxid,timestamp) == 0 )
                {
                    state = 4;
                }
            } else printf("mask.%llx wt.%d\n",(long long)mask,bitweight(mask));
            break;
        case 4: // wait for N/2+1 signed tx and broadcast
            dpow_txidupdate(myinfo,dp,coin,recvmaskp,channel,heightmsg,notaries,numnotaries,myind,hashmsg);
            printf("STATE4\n");
            if ( (m= dpow_mostsignedtx(myinfo,dp,coin,signedtxidp,signedtx,&mask,&k,notaries,numnotaries,heightmsg,myind,hashmsg,btctxid,timestamp)) > 0 )
            {
                if ( m >= numnotaries/2+1 )
                {
                    if ( (retstr= dpow_sendrawtransaction(myinfo,coin,signedtx)) != 0 )
                    {
                        dp->destupdated = 0;
                        printf("sendrawtransaction.(%s)\n",retstr);
                        free(retstr);
                    }
                    state = 0xffffffff;
                }
                else
                {
                    dpow_signedtxgen(myinfo,dp,coin,signedtxidp,signedtx,mask,k,notaries,numnotaries,heightmsg,myind,hashmsg,btctxid,timestamp);
                }
            }
            break;
    }
    return(state);
}

void dpow_statemachinestart(void *ptr)
{
    struct supernet_info *myinfo; struct dpow_info *dp; struct dpow_checkpoint checkpoint; void **ptrs = ptr;
    int32_t i,n,myind = -1; uint64_t recvmask = 0; uint32_t timestamp,srcstate=0,deststate=0; struct iguana_info *src,*dest; struct dpow_hashheight srchash,desthash; char signedtx[16384],str[65],coinaddr[64]; bits256 signedtxid,zero; struct dpow_entry notaries[DPOW_MAXRELAYS];
    memset(&zero,0,sizeof(zero));
    memset(notaries,0,sizeof(notaries));
    myinfo = ptrs[0];
    dp = ptrs[1];
    memcpy(&checkpoint,&ptrs[2],sizeof(checkpoint));
    printf("statemachinestart %s->%s %s ht.%d\n",dp->symbol,dp->dest,bits256_str(str,checkpoint.blockhash.hash),checkpoint.blockhash.height);
    src = iguana_coinfind(dp->symbol);
    dest = iguana_coinfind(dp->dest);
    n = (int32_t)(sizeof(Notaries)/sizeof(*Notaries));
    for (i=0; i<n; i++)
    {
        decode_hex(notaries[i].pubkey,33,Notaries[i][1]);
        bitcoin_address(coinaddr,src->chain->pubtype,notaries[i].pubkey,33);
        printf("%s.%d ",coinaddr,i);
        if ( memcmp(notaries[i].pubkey,myinfo->DPOW.minerkey33,33) == 0 )
            myind = i;
    }
    bitcoin_address(coinaddr,src->chain->pubtype,myinfo->DPOW.minerkey33,33);
    printf(" myaddr.%s\n",coinaddr);
    if ( myind < 0 )
    {
        printf("statemachinestart this node %s is not official notary\n",coinaddr);
        free(ptr);
        return;
    }
    dp->checkpoint = checkpoint;
    timestamp = checkpoint.timestamp;
    srchash = checkpoint.blockhash;
    desthash = dp->notarized[0];
    printf("DPOW statemachine checkpoint.%d %s\n",checkpoint.blockhash.height,bits256_str(str,checkpoint.blockhash.hash));
    while ( src != 0 && dest != 0 && (srcstate != 0xffffffff || deststate != 0xffffffff) )
    {
        sleep(1);
        if ( dp->checkpoint.blockhash.height > checkpoint.blockhash.height )
        {
            printf("abort ht.%d due to new checkpoint.%d\n",checkpoint.blockhash.height,dp->checkpoint.blockhash.height);
            break;
        }
        if ( deststate != 0xffffffff )
        {
            printf("DEST.%08x %s\n",deststate,bits256_str(str,srchash.hash));
            deststate = dpow_statemachineiterate(myinfo,dp,dest,deststate,srchash.hash,srchash.height,zero,notaries,n,myind,&recvmask,&signedtxid,signedtx,timestamp);
        } else printf("deststate.%08x\n",deststate);
        if ( deststate == 0xffffffff )
        {
            if ( srcstate != 0xffffffff )
            {
                printf("SRC.%08x\n",srcstate);
                srcstate = 0xffffffff;//dpow_statemachineiterate(myinfo,dp,src,srcstate,srchash.hash,srchash.height,signedtxid,notaries,n,myind,&recvmask,&signedtxid2,signedtx2,timestamp);
            }
        }
    }
    free(ptr);
}

void dpow_fifoupdate(struct supernet_info *myinfo,struct dpow_checkpoint *fifo,struct dpow_checkpoint tip)
{
    int32_t i; struct dpow_checkpoint newfifo[DPOW_FIFOSIZE]; char str[65];
    memset(newfifo,0,sizeof(newfifo));
    for (i=DPOW_FIFOSIZE-1; i>0; i--)
        newfifo[i] = fifo[i-1];
    newfifo[0] = tip;
    memcpy(fifo,newfifo,sizeof(newfifo));
    for (i=0; i<DPOW_FIFOSIZE; i++)
        printf("%d ",bits256_nonz(fifo[i].blockhash.hash));
    printf(" <- fifo %s\n",bits256_str(str,tip.blockhash.hash));
}

void dpow_checkpointset(struct supernet_info *myinfo,struct dpow_checkpoint *checkpoint,int32_t height,bits256 hash,uint32_t timestamp,uint32_t blocktime)
{
    checkpoint->timestamp = timestamp;
    checkpoint->blocktime = blocktime;
    checkpoint->blockhash.hash = hash;
    checkpoint->blockhash.height = height;
}

void dpow_srcupdate(struct supernet_info *myinfo,struct dpow_info *dp,int32_t height,bits256 hash,uint32_t timestamp,uint32_t blocktime)
{
    void **ptrs; char str[65]; struct dpow_checkpoint checkpoint;
    dpow_checkpointset(myinfo,&dp->last,height,hash,timestamp,blocktime);
    checkpoint = dp->srcfifo[dp->srcconfirms];
    printf("%s srcupdate ht.%d destupdated.%u nonz.%d %s\n",dp->symbol,height,dp->destupdated,bits256_nonz(checkpoint.blockhash.hash),bits256_str(str,dp->last.blockhash.hash));
    dpow_fifoupdate(myinfo,dp->srcfifo,dp->last);
    if ( dp->destupdated != 0 && bits256_nonz(checkpoint.blockhash.hash) != 0 )
    {
        ptrs = calloc(1,sizeof(void *)*2 + sizeof(struct dpow_checkpoint));
        ptrs[0] = (void *)myinfo;
        ptrs[1] = (void *)dp;
        memcpy(&ptrs[2],&checkpoint,sizeof(checkpoint));
        if ( OS_thread_create(malloc(sizeof(pthread_t)),NULL,(void *)dpow_statemachinestart,(void *)ptrs) != 0 )
        {
        }
    }
}

void dpow_approvedset(struct supernet_info *myinfo,struct dpow_info *dp,struct dpow_checkpoint *checkpoint,bits256 *txs,int32_t numtx)
{
    int32_t i,j; bits256 txid;
    if ( txs != 0 )
    {
        for (i=0; i<numtx; i++)
        {
            txid = txs[i];
            if ( bits256_nonz(txid) != 0 )
            {
                for (j=0; j<DPOW_FIFOSIZE; j++)
                {
                    if ( bits256_cmp(txid,dp->approved[j].hash) == 0 )
                    {
                        if ( bits256_nonz(checkpoint->approved.hash) == 0 || dp->approved[j].height >= checkpoint->approved.height )
                            checkpoint->approved = dp->approved[j];
                    }
                }
            }
        }
    }
}

void dpow_destconfirm(struct supernet_info *myinfo,struct dpow_info *dp,struct dpow_checkpoint *checkpoint)
{
    int32_t i;
    if ( bits256_nonz(checkpoint->approved.hash) != 0 )
    {
        for (i=DPOW_FIFOSIZE-1; i>0; i--)
            dp->notarized[i] = dp->notarized[i-1];
        dp->notarized[0] = checkpoint->approved;
    }
}

void dpow_destupdate(struct supernet_info *myinfo,struct dpow_info *dp,int32_t height,bits256 hash,uint32_t timestamp,uint32_t blocktime)
{
    printf("%s destupdate ht.%d\n",dp->dest,height);
    dp->destupdated = timestamp;
    dpow_checkpointset(myinfo,&dp->destchaintip,height,hash,timestamp,blocktime);
    dpow_approvedset(myinfo,dp,&dp->destchaintip,dp->desttx,dp->numdesttx);
    dpow_fifoupdate(myinfo,dp->destfifo,dp->destchaintip);
    if ( strcmp(dp->dest,DPOW_BTCSTR) == 0 )
        dpow_destconfirm(myinfo,dp,&dp->destfifo[DPOW_BTCCONFIRMS]);
    else
    {
        dpow_destconfirm(myinfo,dp,&dp->destfifo[DPOW_KOMODOCONFIRMS * 2]); // todo: change to notarized KMD depth
    }
}

void iguana_dPoWupdate(struct supernet_info *myinfo)
{
    int32_t height; char str[65]; uint32_t blocktime; bits256 blockhash; struct iguana_info *src,*dest; struct dpow_info *dp = &myinfo->DPOW;
    if ( strcmp(dp->symbol,"KMD") == 0 )
    {
        strcpy(dp->dest,DPOW_BTCSTR);
        dp->srcconfirms = DPOW_KOMODOCONFIRMS;
    }
    else
    {
        strcpy(dp->dest,"KMD");
        dp->srcconfirms = DPOW_THIRDPARTY_CONFIRMS;
    }
    if ( dp->srcconfirms > DPOW_FIFOSIZE )
        dp->srcconfirms = DPOW_FIFOSIZE;
    src = iguana_coinfind(dp->symbol);
    dest = iguana_coinfind(dp->dest);
    if ( src != 0 && dest != 0 )
    {
        dp->numdesttx = sizeof(dp->desttx)/sizeof(*dp->desttx);
        if ( (height= dpow_getchaintip(myinfo,&blockhash,&blocktime,dp->desttx,&dp->numdesttx,dest)) != dp->destchaintip.blockhash.height && height >= 0 )
        {
            printf("%s %s height.%d vs last.%d\n",dp->dest,bits256_str(str,blockhash),height,dp->destchaintip.blockhash.height);
            if ( height <= dp->destchaintip.blockhash.height )
            {
                printf("iguana_dPoWupdate dest.%s reorg detected %d vs %d\n",dp->dest,height,dp->destchaintip.blockhash.height);
                if ( height == dp->destchaintip.blockhash.height && bits256_cmp(blockhash,dp->destchaintip.blockhash.hash) != 0 )
                    printf("UNEXPECTED ILLEGAL BLOCK in dest chaintip\n");
            } else dpow_destupdate(myinfo,dp,height,blockhash,(uint32_t)time(NULL),blocktime);
        }
        dp->numsrctx = sizeof(dp->srctx)/sizeof(*dp->srctx);
        if ( (height= dpow_getchaintip(myinfo,&blockhash,&blocktime,dp->srctx,&dp->numsrctx,src)) != dp->last.blockhash.height && height >= 0 )
        {
            printf("%s %s height.%d vs last.%d\n",dp->symbol,bits256_str(str,blockhash),height,dp->last.blockhash.height);
            if ( height < dp->last.blockhash.height )
            {
                printf("iguana_dPoWupdate src.%s reorg detected %d vs %d approved.%d notarized.%d\n",dp->symbol,height,dp->last.blockhash.height,dp->approved[0].height,dp->notarized[0].height);
                if ( height <= dp->approved[0].height )
                {
                    if ( bits256_cmp(blockhash,dp->last.blockhash.hash) != 0 )
                        printf("UNEXPECTED ILLEGAL BLOCK in src chaintip\n");
                } else dpow_srcupdate(myinfo,dp,height,blockhash,(uint32_t)time(NULL),blocktime);
            } else dpow_srcupdate(myinfo,dp,height,blockhash,(uint32_t)time(NULL),blocktime);
        }
    } else printf("iguana_dPoWupdate missing src.(%s) %p or dest.(%s) %p\n",dp->symbol,src,dp->dest,dest);
}

#include "../includes/iguana_apidefs.h"

TWO_STRINGS(iguana,dpow,symbol,pubkey)
{
    if ( myinfo->NOTARY.RELAYID < 0 )
        return(clonestr("{\"error\":\"must be running as notary node\"}"));
    if ( myinfo->DPOW.symbol[0] != 0 )
        return(clonestr("{\"error\":\"cant dPoW more than one coin at a time\"}"));
    if ( pubkey == 0 || pubkey[0] == 0 || is_hexstr(pubkey,0) != 66 )
        return(clonestr("{\"error\":\"need 33 byte pubkey\"}"));
    if ( symbol == 0 || symbol[0] == 0 )
        symbol = "KMD";
    if ( iguana_coinfind(symbol) == 0 )
        return(clonestr("{\"error\":\"cant dPoW an inactive coin\"}"));
    if ( strcmp(symbol,"KMD") == 0 && iguana_coinfind(DPOW_BTCSTR) == 0 )
        return(clonestr("{\"error\":\"cant dPoW KMD without BTC\"}"));
    else if ( strcmp(symbol,"KMD") != 0 && iguana_coinfind("KMD") == 0 )
        return(clonestr("{\"error\":\"cant dPoW without KMD\"}"));
    decode_hex(myinfo->DPOW.minerkey33,33,pubkey);
    if ( bitcoin_pubkeylen(myinfo->DPOW.minerkey33) <= 0 )
        return(clonestr("{\"error\":\"illegal pubkey\"}"));
    strcpy(myinfo->DPOW.symbol,symbol);
    return(clonestr("{\"result\":\"success\"}"));
}

#include "../includes/iguana_apiundefs.h"
