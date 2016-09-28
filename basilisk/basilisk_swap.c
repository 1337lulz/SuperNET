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

// included from basilisk.c
/* https://bitcointalk.org/index.php?topic=1340621.msg13828271#msg13828271
 https://bitcointalk.org/index.php?topic=1364951
 Tier Nolan's approach is followed with the following changes:
 a) instead of cutting 1000 keypairs, only INSTANTDEX_DECKSIZE are a
 b) instead of sending the entire 256 bits, it is truncated to 64 bits. With odds of collision being so low, it is dwarfed by the ~0.1% insurance factor.
 c) D is set to 100x the insurance rate of 1/777 12.87% + BTC amount
 d) insurance is added to Bob's payment, which is after the deposit and bailin
 e) BEFORE Bob broadcasts deposit, Alice broadcasts BTC denominated fee in cltv so if trade isnt done fee is reclaimed
 */

#define DISABLE_CHECKSIG

/*
 both fees are standard payments: OP_DUP OP_HASH160 FEE_RMD160 OP_EQUALVERIFY OP_CHECKSIG
 
 Alice altpayment: OP_2 <alice_pubM> <bob_pubN> OP_2 OP_CHECKMULTISIG
 
 Bob deposit:
 #ifndef DISABLE_CHECKSIG
 OP_IF
 <now + INSTANTDEX_LOCKTIME*2> OP_CLTV OP_DROP <alice_pubA0> OP_CHECKSIG
 OP_ELSE
 OP_HASH160 <hash(bob_privN)> OP_EQUALVERIFY <bob_pubB0> OP_CHECKSIG
 OP_ENDIF
 #else
 OP_IF
 <now + INSTANTDEX_LOCKTIME*2> OP_CLTV OP_DROP OP_SHA256 <sha256(alice_privA0)> OP_EQUAL
 OP_ELSE
 OP_HASH160 <hash(bob_privN)> OP_EQUALVERIFY OP_SHA256 <sha256(bob_privB0)> OP_EQUAL
 OP_ENDIF
#endif
 
 Bob paytx:
 #ifndef DISABLE_CHECKSIG
 OP_IF
 <now + INSTANTDEX_LOCKTIME> OP_CLTV OP_DROP <bob_pubB1> OP_CHECKSIG
 OP_ELSE
 OP_HASH160 <hash(alice_privM)> OP_EQUALVERIFY <alice_pubA0> OP_CHECKSIG
 OP_ENDIF
 #else
 OP_IF
 <now + INSTANTDEX_LOCKTIME> OP_CLTV OP_DROP OP_SHA256 <sha256(bob_privB1)> OP_EQUAL
 OP_ELSE
 OP_HASH160 <hash(alice_privM)> OP_EQUALVERIFY OP_SHA256 <sha256(alice_privA0)> OP_EQUAL
 OP_ENDIF
 #endif
 
 Naming convention are pubAi are alice's pubkeys (seems only pubA0 and not pubA1)
 pubBi are Bob's pubkeys
 
 privN is Bob's privkey from the cut and choose deck as selected by Alice
 privM is Alice's counterpart
 pubN and pubM are the corresponding pubkeys for these chosen privkeys
 
 Alice timeout event is triggered if INSTANTDEX_LOCKTIME elapses from the start of a FSM instance. Bob timeout event is triggered after INSTANTDEX_LOCKTIME*2
 */

/*
Bob sends bobdeposit and waits for alicepayment to confirm before sending bobpayment
Alice waits for bobdeposit to confirm and sends alicepayment

Alice spends bobpayment immediately divulging privAm
Bob spends alicepayment immediately after getting privAm and divulges privBn

Bob will spend bobdeposit after end of trade or INSTANTDEX_LOCKTIME, divulging privBn
Alice spends alicepayment as soon as privBn is seen

Bob will spend bobpayment after INSTANTDEX_LOCKTIME
Alice spends bobdeposit in 2*INSTANTDEX_LOCKTIME
*/

void basilisk_txlog(struct supernet_info *myinfo,struct basilisk_swap *swap,struct basilisk_rawtx *rawtx,int32_t delay)
{
    // save in append only for backstop reclaiming
}

void revcalc_rmd160_sha256(uint8_t rmd160[20],bits256 revhash)
{
    bits256 hash; int32_t i;
    for (i=0; i<32; i++)
        hash.bytes[i] = revhash.bytes[31-i];
    calc_rmd160_sha256(rmd160,hash.bytes,sizeof(hash));
}

bits256 revcalc_sha256(bits256 revhash)
{
    bits256 hash,dest; int32_t i;
    for (i=0; i<32; i++)
        hash.bytes[i] = revhash.bytes[31-i];
    vcalc_sha256(0,dest.bytes,hash.bytes,sizeof(hash));
    return(dest);
}

#define SCRIPT_OP_IF 0x63
#define SCRIPT_OP_ELSE 0x67
#define SCRIPT_OP_ENDIF 0x68

bits256 basilisk_revealkey(bits256 privkey,bits256 pubkey)
{
    bits256 reveal;
#ifdef DISABLE_CHECKSIG
    vcalc_sha256(0,reveal.bytes,privkey.bytes,sizeof(privkey));
    //reveal = revcalc_sha256(privkey);
    char str[65],str2[65]; printf("priv.(%s) -> reveal.(%s)\n",bits256_str(str,privkey),bits256_str(str2,reveal));
#else
    reveal = pubkey;
#endif
    return(reveal);
}

int32_t basilisk_bobscript(uint8_t *rmd160,uint8_t *redeemscript,int32_t *redeemlenp,uint8_t *script,int32_t n,uint32_t *locktimep,int32_t *secretstartp,struct basilisk_swap *swap,int32_t depositflag)
{
    uint8_t pubkeyA[33],pubkeyB[33],*secret160,*secret256; bits256 privkey,cltvpub,destpub; int32_t i;
    *locktimep = swap->locktime;
    if ( depositflag != 0 )
    {
        *locktimep += INSTANTDEX_LOCKTIME;
        pubkeyA[0] = 0x02, cltvpub = swap->pubA0;
        pubkeyB[0] = 0x03, destpub = swap->pubB0;
        privkey = swap->privBn;
        secret160 = swap->secretBn;
        secret256 = swap->secretBn256;
    }
    else
    {
        pubkeyA[0] = 0x03, cltvpub = swap->pubB1;
        pubkeyB[0] = 0x02, destpub = swap->pubA0;
        privkey = swap->privAm;
        secret160 = swap->secretAm;
        secret256 = swap->secretAm256;
    }
    for (i=0; i<32; i++)
        printf("%02x",secret256[i]);
    printf(" <- secret256 depositflag.%d\n",depositflag);
    if ( bits256_nonz(cltvpub) == 0 || bits256_nonz(destpub) == 0 )
        return(-1);
    for (i=0; i<20; i++)
        if ( secret160[i] != 0 )
            break;
    if ( i == 20 )
        return(-1);
    memcpy(pubkeyA+1,cltvpub.bytes,sizeof(cltvpub));
    memcpy(pubkeyB+1,destpub.bytes,sizeof(destpub));
    redeemscript[n++] = SCRIPT_OP_IF;
    n = bitcoin_checklocktimeverify(redeemscript,n,*locktimep);
#ifdef DISABLE_CHECKSIG
    n = bitcoin_secret256spend(redeemscript,n,cltvpub);
#else
    n = bitcoin_pubkeyspend(redeemscript,n,pubkeyA);
#endif
    redeemscript[n++] = SCRIPT_OP_ELSE;
    if ( secretstartp != 0 )
        *secretstartp = n + 2;
    if ( 1 )
    {
        if ( 1 && bits256_nonz(privkey) != 0 )
        {
            uint8_t bufA[20],bufB[20];
            revcalc_rmd160_sha256(bufA,privkey);
            calc_rmd160_sha256(bufB,privkey.bytes,sizeof(privkey));
            if ( memcmp(bufA,secret160,sizeof(bufA)) == 0 )
                printf("MATCHES BUFA\n");
            else if ( memcmp(bufB,secret160,sizeof(bufB)) == 0 )
                printf("MATCHES BUFB\n");
            else printf("secret160 matches neither\n");
            memcpy(secret160,bufB,20);
        }
        n = bitcoin_secret160verify(redeemscript,n,secret160);
    }
    else
    {
        redeemscript[n++] = 0xa8;//IGUANA_OP_SHA256;
        redeemscript[n++] = 0x20;
        memcpy(&redeemscript[n],secret256,0x20), n += 0x20;
        redeemscript[n++] = 0x88; //SCRIPT_OP_EQUALVERIFY;
    }
#ifdef DISABLE_CHECKSIG
    n = bitcoin_secret256spend(redeemscript,n,destpub);
#else
    n = bitcoin_pubkeyspend(redeemscript,n,pubkeyB);
#endif
    redeemscript[n++] = SCRIPT_OP_ENDIF;
    *redeemlenp = n;
    calc_rmd160_sha256(rmd160,redeemscript,n);
    n = bitcoin_p2shspend(script,0,rmd160);
    for (i=0; i<n; i++)
        printf("%02x",script[i]);
    char str[65]; printf(" <- redeem.%d bobtx dflag.%d %s\n",n,depositflag,bits256_str(str,cltvpub));
    return(n);
}

int32_t basilisk_alicescript(uint8_t *redeemscript,int32_t *redeemlenp,uint8_t *script,int32_t n,char *msigaddr,uint8_t altps2h,bits256 pubAm,bits256 pubBn)
{
    uint8_t p2sh160[20]; struct vin_info V;
    memset(&V,0,sizeof(V));
    memcpy(&V.signers[0].pubkey[1],pubAm.bytes,sizeof(pubAm)), V.signers[0].pubkey[0] = 0x02;
    memcpy(&V.signers[1].pubkey[1],pubBn.bytes,sizeof(pubBn)), V.signers[1].pubkey[0] = 0x03;
    V.M = V.N = 2;
    *redeemlenp = bitcoin_MofNspendscript(p2sh160,redeemscript,n,&V);
    bitcoin_address(msigaddr,altps2h,p2sh160,sizeof(p2sh160));
    n = bitcoin_p2shspend(script,0,p2sh160);
    //for (i=0; i<*redeemlenp; i++)
    //    printf("%02x",redeemscript[i]);
    //printf(" <- redeemscript alicetx\n");
    return(n);
}

int32_t basilisk_confirmsobj(cJSON *item)
{
    int32_t height,numconfirms;
    height = jint(item,"height");
    numconfirms = jint(item,"numconfirms");
    if ( height > 0 && numconfirms >= 0 )
        return(numconfirms);
    return(-1);
}

int32_t basilisk_numconfirms(struct supernet_info *myinfo,struct basilisk_rawtx *rawtx)
{
    cJSON *argjson,*valuearray=0; char *valstr; int32_t i,n,retval = -1;
#ifdef BASILISK_DISABLEWAITTX
    return(10);
#endif
    argjson = cJSON_CreateObject();
    jaddbits256(argjson,"txid",rawtx->actualtxid);
    jaddnum(argjson,"vout",0);
    jaddstr(argjson,"coin",rawtx->coin->symbol);
    if ( (valstr= basilisk_value(myinfo,rawtx->coin,0,0,myinfo->myaddr.persistent,argjson,0)) != 0 )
    {
        //char str[65]; printf("%s %s valstr.(%s)\n",rawtx->name,bits256_str(str,rawtx->actualtxid),valstr);
        if ( (valuearray= cJSON_Parse(valstr)) != 0 )
        {
            if ( is_cJSON_Array(valuearray) != 0 )
            {
                n = cJSON_GetArraySize(valuearray);
                for (i=0; i<n; i++)
                {
                    if ( (retval= basilisk_confirmsobj(jitem(valuearray,i))) >= 0 )
                        break;
               }
            } else retval = basilisk_confirmsobj(valuearray);
            free_json(valuearray);
        } else printf("parse error\n");
        free(valstr);
    }
    free_json(argjson);
    return(retval);
}

bits256 basilisk_swap_broadcast(char *name,struct supernet_info *myinfo,struct basilisk_swap *swap,struct iguana_info *coin,uint8_t *data,int32_t datalen)
{
    bits256 txid; char *signedtx;
    memset(txid.bytes,0,sizeof(txid));
    if ( data != 0 && datalen != 0 )
    {
        char str[65];
#ifdef BASILISK_DISABLESENDTX
        txid = bits256_doublesha256(0,data,datalen);
        printf("%s <- dont sendrawtransaction (%s)\n",name,bits256_str(str,txid));
        return(txid);
#endif
        signedtx = malloc(datalen*2 + 1);
        init_hexbytes_noT(signedtx,data,datalen);
        txid = iguana_sendrawtransaction(myinfo,coin,signedtx);
        printf("sendrawtransaction %s.(%s)\n",name,bits256_str(str,txid));
        free(signedtx);
    }
    return(txid);
}

int32_t basilisk_rawtx_sign(struct supernet_info *myinfo,int32_t height,struct basilisk_swap *swap,struct basilisk_rawtx *dest,struct basilisk_rawtx *rawtx,bits256 privkey,bits256 *privkey2,uint8_t *userdata,int32_t userdatalen)
{
    char *rawtxbytes=0,*signedtx=0,hexstr[999],wifstr[128]; cJSON *txobj,*vins,*item,*sobj,*privkeys; int32_t needsig=1,retval = -1; struct vin_info *V; uint32_t locktime=0;
    V = calloc(16,sizeof(*V));
    if ( dest == &swap->aliceclaim )
        locktime = swap->locktime + INSTANTDEX_LOCKTIME;
    V[0].signers[0].privkey = privkey;
    bitcoin_pubkey33(myinfo->ctx,V[0].signers[0].pubkey,privkey);
    privkeys = cJSON_CreateArray();
    bitcoin_priv2wif(wifstr,privkey,rawtx->coin->chain->wiftype);
    jaddistr(privkeys,wifstr);
    if ( privkey2 != 0 )
    {
        V[0].signers[1].privkey = *privkey2;
        bitcoin_pubkey33(myinfo->ctx,V[0].signers[1].pubkey,*privkey2);
        bitcoin_priv2wif(wifstr,*privkey2,rawtx->coin->chain->wiftype);
        jaddistr(privkeys,wifstr);
        V[0].N = V[0].M = 2;
        //char str[65]; printf("add second privkey.(%s) %s\n",jprint(privkeys,0),bits256_str(str,*privkey2));
    } else V[0].N = V[0].M = 1;
    V[0].suppress_pubkeys = dest->suppress_pubkeys;
    if ( dest->redeemlen != 0 )
        memcpy(V[0].p2shscript,dest->redeemscript,dest->redeemlen), V[0].p2shlen = dest->redeemlen;
    txobj = bitcoin_txcreate(rawtx->coin->chain->isPoS,locktime,userdata == 0 ? 1 : 1);//rawtx->coin->chain->locktime_txversion);
    vins = cJSON_CreateArray();
    item = cJSON_CreateObject();
    if ( userdata != 0 && userdatalen > 0 )
    {
        memcpy(V[0].userdata,userdata,userdatalen);
        V[0].userdatalen = userdatalen;
        init_hexbytes_noT(hexstr,userdata,userdatalen);
        jaddstr(item,"userdata",hexstr);
        //jaddnum(item,"sequence",0);
#ifdef DISABLE_CHECKSIG
        needsig = 0;
#endif
   }
    if ( bits256_nonz(rawtx->actualtxid) != 0 )
        jaddbits256(item,"txid",rawtx->actualtxid);
    else jaddbits256(item,"txid",rawtx->signedtxid);
    jaddnum(item,"vout",0);
    sobj = cJSON_CreateObject();
    init_hexbytes_noT(hexstr,rawtx->spendscript,rawtx->spendlen);
    jaddstr(sobj,"hex",hexstr);
    jadd(item,"scriptPubKey",sobj);
    jaddnum(item,"suppress",dest->suppress_pubkeys);
    //if ( locktime != 0 )
    //    jaddnum(item,"sequence",0);
    if ( (dest->redeemlen= rawtx->redeemlen) != 0 )
    {
        init_hexbytes_noT(hexstr,rawtx->redeemscript,rawtx->redeemlen);
        memcpy(dest->redeemscript,rawtx->redeemscript,rawtx->redeemlen);
        jaddstr(item,"redeemScript",hexstr);
    }
    jaddi(vins,item);
    jdelete(txobj,"vin");
    jadd(txobj,"vin",vins);
    //printf("basilisk_rawtx_sign locktime.%u/%u for %s spendscript.%s -> %s, suppress.%d\n",rawtx->locktime,dest->locktime,rawtx->name,hexstr,dest->name,dest->suppress_pubkeys);
    txobj = bitcoin_txoutput(txobj,dest->spendscript,dest->spendlen,dest->amount);
    if ( (rawtxbytes= bitcoin_json2hex(myinfo,rawtx->coin,&dest->txid,txobj,V)) != 0 )
    {
        if ( needsig == 0 )
            signedtx = rawtxbytes;
        if ( signedtx != 0 || (signedtx= iguana_signrawtx(myinfo,rawtx->coin,height,&dest->signedtxid,&dest->completed,vins,rawtxbytes,privkeys,V)) != 0 )
        {
            dest->datalen = (int32_t)strlen(signedtx) >> 1;
            dest->txbytes = calloc(1,dest->datalen);
            decode_hex(dest->txbytes,dest->datalen,signedtx);
            if ( signedtx != rawtxbytes )
                free(signedtx);
            retval = 0;
        } else printf("error signing\n");
        free(rawtxbytes);
    } else printf("error making rawtx\n");
    free_json(privkeys);
    free_json(txobj);
    return(retval);
}

struct basilisk_rawtx *basilisk_swapdata_rawtx(struct supernet_info *myinfo,struct basilisk_swap *swap,uint8_t *data,int32_t maxlen,struct basilisk_rawtx *rawtx)
{
    if ( rawtx->txbytes != 0 && rawtx->datalen <= maxlen )
    {
        memcpy(data,rawtx->txbytes,rawtx->datalen);
        return(rawtx);
    }
    return(0);
}

int32_t basilisk_verify_otherfee(struct supernet_info *myinfo,void *ptr,uint8_t *data,int32_t datalen)
{
    struct basilisk_swap *swap = ptr;
    // add verification and broadcast
    swap->otherfee.txbytes = calloc(1,datalen);
    memcpy(swap->otherfee.txbytes,data,datalen);
    swap->otherfee.actualtxid = swap->otherfee.signedtxid = bits256_doublesha256(0,data,datalen);
    basilisk_txlog(myinfo,swap,&swap->otherfee,-1);
    return(0);
}

int32_t basilisk_rawtx_spendscript(struct supernet_info *myinfo,struct basilisk_swap *swap,int32_t height,struct basilisk_rawtx *rawtx,int32_t v,uint8_t *recvbuf,int32_t recvlen,int32_t suppress_pubkeys)
{
    int32_t datalen=0,retval=-1,hexlen,n; uint8_t *data; cJSON *txobj,*skey,*vouts,*vout; char *hexstr;
    datalen = recvbuf[0];
    datalen += (int32_t)recvbuf[1] << 8;
    if ( datalen > 65536 )
        return(-1);
    rawtx->redeemlen = recvbuf[2];
    data = &recvbuf[3];
    if ( rawtx->redeemlen > 0 && rawtx->redeemlen < 0x100 )
        memcpy(rawtx->redeemscript,&data[datalen],rawtx->redeemlen);
    //printf("recvlen.%d datalen.%d redeemlen.%d\n",recvlen,datalen,rawtx->redeemlen);
    if ( rawtx->txbytes == 0 )
    {
        rawtx->txbytes = calloc(1,datalen);
        memcpy(rawtx->txbytes,data,datalen);
        rawtx->datalen = datalen;
    }
    else if ( datalen != rawtx->datalen || memcmp(rawtx->txbytes,data,datalen) != 0 )
    {
        int32_t i; for (i=0; i<datalen; i++)
            printf("%02x",data[i]);
        printf(" <- received\n");
        for (i=0; i<rawtx->datalen; i++)
            printf("%02x",rawtx->txbytes[i]);
        printf(" <- rawtx\n");
        printf("%s rawtx data compare error, len %d vs %d <<<<<<<<<< warning\n",rawtx->name,rawtx->datalen,datalen);
        return(-1);
    }
    if ( (txobj= bitcoin_data2json(rawtx->coin,height,&rawtx->signedtxid,&rawtx->msgtx,rawtx->extraspace,sizeof(rawtx->extraspace),data,datalen,0,suppress_pubkeys)) != 0 )
    {
        rawtx->actualtxid = rawtx->signedtxid;
        //char str[65]; printf("got txid.%s (%s)\n",bits256_str(str,rawtx->signedtxid),jprint(txobj,0));
        rawtx->locktime = rawtx->msgtx.lock_time;
        if ( (vouts= jarray(&n,txobj,"vout")) != 0 && v < n )
        {
            vout = jitem(vouts,v);
            if ( j64bits(vout,"satoshis") == rawtx->amount && (skey= jobj(vout,"scriptPubKey")) != 0 && (hexstr= jstr(skey,"hex")) != 0 )
            {
                if ( (hexlen= (int32_t)strlen(hexstr) >> 1) < sizeof(rawtx->spendscript) )
                {
                    decode_hex(rawtx->spendscript,hexlen,hexstr);
                    rawtx->spendlen = hexlen;
                    basilisk_txlog(myinfo,swap,rawtx,-1); // bobdeposit, bobpayment or alicepayment
                    retval = 0;
                }
            } else printf("%s ERROR.(%s)\n",rawtx->name,jprint(txobj,0));
        }
        free_json(txobj);
    }
    return(retval);
}

int32_t basilisk_swapuserdata(struct basilisk_swap *swap,uint8_t *userdata,bits256 privkey,int32_t ifpath,bits256 signpriv,uint8_t *redeemscript,int32_t redeemlen)
{
    int32_t i,len = 0;
#ifdef DISABLE_CHECKSIG
    userdata[len++] = sizeof(signpriv);
    for (i=0; i<sizeof(privkey); i++)
        userdata[len++] = signpriv.bytes[i];
#endif
    if ( bits256_nonz(privkey) != 0 )
    {
        userdata[len++] = sizeof(privkey);
        for (i=0; i<sizeof(privkey); i++)
            userdata[len++] = privkey.bytes[i];
    }
    userdata[len++] = 0x51 * ifpath; // ifpath == 1 -> if path, 0 -> else path
    return(len);
}

/*    Bob deposit:
 OP_IF
 <now + INSTANTDEX_LOCKTIME*2> OP_CLTV OP_DROP <alice_pubA0> OP_CHECKSIG
 OP_ELSE
 OP_HASH160 <hash(bob_privN)> OP_EQUALVERIFY <bob_pubB0> OP_CHECKSIG
 OP_ENDIF*/

int32_t basilisk_verify_bobdeposit(struct supernet_info *myinfo,void *ptr,uint8_t *data,int32_t datalen)
{
    uint8_t userdata[512]; int32_t retval,len = 0; static bits256 zero; struct basilisk_swap *swap = ptr;
    if ( basilisk_rawtx_spendscript(myinfo,swap,swap->bobcoin->blocks.hwmchain.height,&swap->bobdeposit,0,data,datalen,0) == 0 )
    {
        //userdata[len++] = 0x51;
        //basilisk_bobscripts_set(myinfo,swap,1,0);
        len = basilisk_swapuserdata(swap,userdata,zero,1,swap->myprivs[0],swap->bobdeposit.redeemscript,swap->bobdeposit.redeemlen);
        if ( (retval= basilisk_rawtx_sign(myinfo,swap->bobcoin->blocks.hwmchain.height,swap,&swap->aliceclaim,&swap->bobdeposit,swap->myprivs[0],0,userdata,len)) == 0 )
        {
            basilisk_txlog(myinfo,swap,&swap->aliceclaim,INSTANTDEX_LOCKTIME*2);
            return(retval);
        }
    }
    printf("error with bobdeposit\n");
    return(-1);
}

int32_t basilisk_bobdeposit_refund(struct supernet_info *myinfo,struct basilisk_swap *swap,int32_t delay)
{
    uint8_t userdata[512]; int32_t i,retval,len = 0; char str[65];
    len = basilisk_swapuserdata(swap,userdata,swap->privBn,0,swap->myprivs[0],swap->bobdeposit.redeemscript,swap->bobdeposit.redeemlen);
    if ( (retval= basilisk_rawtx_sign(myinfo,swap->bobcoin->blocks.hwmchain.height,swap,&swap->bobrefund,&swap->bobdeposit,swap->myprivs[0],0,userdata,len)) == 0 )
    {
        for (i=0; i<swap->bobrefund.datalen; i++)
            printf("%02x",swap->bobrefund.txbytes[i]);
        printf(" <- bobrefund.(%s)\n",bits256_str(str,swap->bobrefund.txid));
        basilisk_txlog(myinfo,swap,&swap->bobrefund,delay);
        return(retval);
    }
    return(-1);
}

/*Bob paytx:
 OP_IF
 <now + INSTANTDEX_LOCKTIME> OP_CLTV OP_DROP <bob_pubB1> OP_CHECKSIG
 OP_ELSE
 OP_HASH160 <hash(alice_privM)> OP_EQUALVERIFY <alice_pubA0> OP_CHECKSIG
 OP_ENDIF*/

int32_t basilisk_bobpayment_reclaim(struct supernet_info *myinfo,struct basilisk_swap *swap,int32_t delay)
{
    uint8_t userdata[512]; int32_t retval,len = 0; static bits256 zero;
    printf("basilisk_bobpayment_reclaim\n");
    len = basilisk_swapuserdata(swap,userdata,zero,1,swap->myprivs[1],swap->bobpayment.redeemscript,swap->bobpayment.redeemlen);
    //userdata[len++] = 0x51;
    if ( (retval= basilisk_rawtx_sign(myinfo,swap->bobcoin->blocks.hwmchain.height,swap,&swap->bobreclaim,&swap->bobpayment,swap->myprivs[1],0,userdata,len)) == 0 )
    {
        basilisk_txlog(myinfo,swap,&swap->bobreclaim,delay);
        return(retval);
    }
    return(-1);
}

int32_t basilisk_verify_bobpaid(struct supernet_info *myinfo,void *ptr,uint8_t *data,int32_t datalen)
{
    uint8_t userdata[512]; int32_t i,retval,len = 0; bits256 revAm; struct basilisk_swap *swap = ptr;
    if ( basilisk_rawtx_spendscript(myinfo,swap,swap->bobcoin->blocks.hwmchain.height,&swap->bobpayment,0,data,datalen,0) == 0 )
    {
        for (i=0; i<32; i++)
            revAm.bytes[i] = swap->privAm.bytes[31-i];
        len = basilisk_swapuserdata(swap,userdata,revAm,0,swap->myprivs[0],swap->bobpayment.redeemscript,swap->bobpayment.redeemlen);
        char str[65]; printf("bobpaid.(%s)\n",bits256_str(str,swap->privAm));
        if ( (retval= basilisk_rawtx_sign(myinfo,swap->bobcoin->blocks.hwmchain.height,swap,&swap->alicespend,&swap->bobpayment,swap->myprivs[0],0,userdata,len)) == 0 )
        {
            for (i=0; i<swap->alicespend.datalen; i++)
                printf("%02x",swap->alicespend.txbytes[i]);
            printf(" <- alicespend\n\n");
            basilisk_txlog(myinfo,swap,&swap->alicespend,-1);
            return(retval);
        }
    }
    return(-1);
}

int32_t basilisk_alicepayment_spend(struct supernet_info *myinfo,struct basilisk_swap *swap,struct basilisk_rawtx *dest)
{
    int32_t retval;
    //printf("alicepayment_spend\n");
    swap->alicepayment.spendlen = basilisk_alicescript(swap->alicepayment.redeemscript,&swap->alicepayment.redeemlen,swap->alicepayment.spendscript,0,swap->alicepayment.destaddr,swap->alicecoin->chain->p2shtype,swap->pubAm,swap->pubBn);
    if ( (retval= basilisk_rawtx_sign(myinfo,swap->alicecoin->blocks.hwmchain.height,swap,dest,&swap->alicepayment,swap->privAm,&swap->privBn,0,0)) == 0 )
    {
        basilisk_txlog(myinfo,swap,dest,0); // bobspend or alicereclaim
        return(retval);
    }
    return(-1);
}

int32_t basilisk_verify_alicepaid(struct supernet_info *myinfo,void *ptr,uint8_t *data,int32_t datalen)
{
    struct basilisk_swap *swap = ptr;
    if ( basilisk_rawtx_spendscript(myinfo,swap,swap->alicecoin->blocks.hwmchain.height,&swap->alicepayment,0,data,datalen,0) == 0 )
        return(0);
    else return(-1);
}

int32_t basilisk_verify_pubpair(int32_t *wrongfirstbytep,struct basilisk_swap *swap,int32_t ind,uint8_t pub0,bits256 pubi,uint64_t txid)
{
    if ( pub0 != (swap->iambob ^ 1) + 0x02 )
    {
        (*wrongfirstbytep)++;
        printf("wrongfirstbyte[%d] %02x\n",ind,pub0);
        return(-1);
    }
    else if ( swap->otherdeck[ind][1] != pubi.txid )
    {
        printf("otherdeck[%d] priv ->pub mismatch %llx != %llx\n",ind,(long long)swap->otherdeck[ind][1],(long long)pubi.txid);
        return(-1);
    }
    else if ( swap->otherdeck[ind][0] != txid )
    {
        printf("otherdeck[%d] priv mismatch %llx != %llx\n",ind,(long long)swap->otherdeck[ind][0],(long long)txid);
        return(-1);
    }
    return(0);
}

cJSON *basilisk_privkeyarray(struct supernet_info *myinfo,struct iguana_info *coin,cJSON *vins)
{
    cJSON *privkeyarray,*item,*sobj; struct iguana_waddress *waddr; struct iguana_waccount *wacct; char coinaddr[64],account[128],wifstr[64],str[65],*hexstr; uint8_t script[1024]; int32_t i,n,len,vout; bits256 txid;
    privkeyarray = cJSON_CreateArray();
    //printf("%s persistent.(%s) (%s) change.(%s) scriptstr.(%s)\n",coin->symbol,myinfo->myaddr.BTC,coinaddr,coin->changeaddr,scriptstr);
    if ( (n= cJSON_GetArraySize(vins)) > 0 )
    {
        for (i=0; i<n; i++)
        {
            item = jitem(vins,i);
            txid = jbits256(item,"txid");
            vout = jint(item,"vout");
            if ( bits256_nonz(txid) != 0 && vout >= 0 )
            {
                iguana_txidcategory(myinfo,coin,account,coinaddr,txid,vout);
                if ( coinaddr[0] == 0 && (sobj= jobj(item,"scriptPubKey")) != 0 && (hexstr= jstr(sobj,"hex")) != 0 && is_hexstr(hexstr,0) > 0 )
                {
                    len = (int32_t)strlen(hexstr) >> 1;
                    if ( len < (sizeof(script) << 1) )
                    {
                        decode_hex(script,len,hexstr);
                        if ( len == 25 && script[0] == 0x76 && script[1] == 0xa9 && script[2] == 0x14 )
                            bitcoin_address(coinaddr,coin->chain->pubtype,script+3,20);
                    }
                }
                if ( coinaddr[0] != 0 )
                {
                    if ( (waddr= iguana_waddresssearch(myinfo,&wacct,coinaddr)) != 0 )
                    {
                        bitcoin_priv2wif(wifstr,waddr->privkey,coin->chain->wiftype);
                        jaddistr(privkeyarray,waddr->wifstr);
                    } else printf("cant find (%s) in wallet\n",coinaddr);
                } else printf("cant coinaddr from (%s).v%d\n",bits256_str(str,txid),vout);
            } else printf("invalid txid/vout %d of %d\n",i,n);
        }
    }
    return(privkeyarray);
}

int32_t basilisk_rawtx_return(struct supernet_info *myinfo,int32_t height,struct basilisk_rawtx *rawtx,cJSON *item,int32_t lockinputs,struct vin_info *V)
{
    char *signedtx,*txbytes; cJSON *vins,*privkeyarray; int32_t i,n,retval = -1;
    if ( (txbytes= jstr(item,"rawtx")) != 0 && (vins= jobj(item,"vins")) != 0 )
    {
        privkeyarray = basilisk_privkeyarray(myinfo,rawtx->coin,vins);
        if ( (signedtx= iguana_signrawtx(myinfo,rawtx->coin,height,&rawtx->signedtxid,&rawtx->completed,vins,txbytes,privkeyarray,V)) != 0 )
        {
            if ( lockinputs != 0 )
            {
                iguana_RTunspentslock(myinfo,rawtx->coin,vins);
                if ( (n= cJSON_GetArraySize(vins)) != 0 )
                {
                    bits256 txid; int32_t vout;
                    for (i=0; i<n; i++)
                    {
                        item = jitem(vins,i);
                        txid = jbits256(item,"txid");
                        vout = jint(item,"vout");
                    }
                }
            }
            rawtx->datalen = (int32_t)strlen(signedtx) >> 1;
            rawtx->txbytes = calloc(1,rawtx->datalen);
            decode_hex(rawtx->txbytes,rawtx->datalen,signedtx);
            //printf("SIGNEDTX.(%s)\n",signedtx);
            free(signedtx);
            retval = 0;
        } else printf("error signrawtx\n"); //do a very short timeout so it finishes via local poll
        free_json(privkeyarray);
    }
    return(retval);
}

int32_t basilisk_rawtx_gen(char *str,struct supernet_info *myinfo,int32_t iambob,int32_t lockinputs,struct basilisk_rawtx *rawtx,uint32_t locktime,uint8_t *script,int32_t scriptlen,int64_t txfee,int32_t minconf)
{
    char *retstr,scriptstr[1024]; uint32_t basilisktag; int32_t flag,i,n,retval = -1; cJSON *valsobj,*retarray=0; struct vin_info *V;
    //bitcoin_address(coinaddr,rawtx->coin->chain->pubtype,myinfo->persistent_pubkey33,33);
    if ( rawtx->coin->changeaddr[0] == 0 )
    {
        bitcoin_address(rawtx->coin->changeaddr,rawtx->coin->chain->pubtype,myinfo->persistent_pubkey33,33);
        printf("set change address.(%s)\n",rawtx->coin->changeaddr);
    }
    init_hexbytes_noT(scriptstr,script,scriptlen);
    basilisktag = (uint32_t)rand();
    valsobj = cJSON_CreateObject();
    jaddstr(valsobj,"coin",rawtx->coin->symbol);
    jaddstr(valsobj,"spendscript",scriptstr);
    jaddstr(valsobj,"changeaddr",rawtx->coin->changeaddr);
    jadd64bits(valsobj,"satoshis",rawtx->amount);
    jadd64bits(valsobj,"txfee",txfee);
    jaddnum(valsobj,"minconf",minconf);
    jaddnum(valsobj,"locktime",locktime);
    jaddnum(valsobj,"timeout",30000);
    rawtx->locktime = locktime;
    //printf("%s locktime.%u\n",rawtx->name,locktime);
    V = calloc(256,sizeof(*V));
    if ( (retstr= basilisk_bitcoinrawtx(myinfo,rawtx->coin,"",basilisktag,jint(valsobj,"timeout"),valsobj,V)) != 0 )
    {
        //printf("%s %s basilisk_bitcoinrawtx.(%s)\n",rawtx->name,str,retstr);
        flag = 0;
        if ( (retarray= cJSON_Parse(retstr)) != 0 )
        {
            if ( is_cJSON_Array(retarray) != 0 )
            {
                n = cJSON_GetArraySize(retarray);
                for (i=0; i<n; i++)
                {
                    if ( (retval= basilisk_rawtx_return(myinfo,rawtx->coin->blocks.hwmchain.height,rawtx,jitem(retarray,i),lockinputs,V)) == 0 )
                    {
                        rawtx->vins = jobj(jitem(retarray,i),"vins");
                        break;
                    }
                }
            }
            else
            {
                retval = basilisk_rawtx_return(myinfo,rawtx->coin->blocks.hwmchain.height,rawtx,retarray,lockinputs,V);
                rawtx->vins = jobj(retarray,"vins");
            }
            free(retarray);
        } else printf("error parsing.(%s)\n",retstr);
        free(retstr);
    } else printf("error creating %s feetx\n",iambob != 0 ? "BOB" : "ALICE");
    free_json(valsobj);
    free(V);
    return(retval);
}

void basilisk_bobscripts_set(struct supernet_info *myinfo,struct basilisk_swap *swap,int32_t depositflag,int32_t genflag)
{
    int32_t i,j; char str[65];
    if ( genflag != 0 && swap->iambob == 0 )
        printf("basilisk_bobscripts_set WARNING: alice generating BOB tx\n");
    if ( depositflag == 0 )
    {
        swap->bobpayment.spendlen = basilisk_bobscript(swap->bobpayment.rmd160,swap->bobpayment.redeemscript,&swap->bobpayment.redeemlen,swap->bobpayment.spendscript,0,&swap->bobpayment.locktime,&swap->bobpayment.secretstart,swap,0);
        //for (i=0; i<swap->bobpayment.redeemlen; i++)
        //    printf("%02x",swap->bobpayment.redeemscript[i]);
        //printf(" <- bobpayment.%d\n",i);
        if ( genflag != 0 && bits256_nonz(*(bits256 *)swap->secretBn256) != 0 && swap->bobpayment.txbytes == 0 )
        {
            for (i=0; i<3; i++)
            {
                basilisk_rawtx_gen("payment",myinfo,1,1,&swap->bobpayment,swap->bobpayment.locktime,swap->bobpayment.spendscript,swap->bobpayment.spendlen,swap->bobpayment.coin->chain->txfee,1);
                if ( swap->bobpayment.txbytes == 0 || swap->bobpayment.spendlen == 0 )
                {
                    printf("error bob generating %p payment.%d\n",swap->bobpayment.txbytes,swap->bobpayment.spendlen);
                    sleep(3);
                }
                else
                {
                    for (j=0; j<swap->bobpayment.datalen; j++)
                        printf("%02x",swap->bobpayment.txbytes[j]);
                    printf(" <- bobpayment.%d\n",swap->bobpayment.datalen);
                    for (j=0; j<swap->bobpayment.redeemlen; j++)
                        printf("%02x",swap->bobpayment.redeemscript[j]);
                    printf(" <- redeem.%d\n",swap->bobpayment.redeemlen);
                    printf("GENERATED BOB PAYMENT.(%s)\n",bits256_str(str,swap->bobpayment.txid));
                    iguana_unspents_mark(myinfo,swap->bobcoin,swap->bobpayment.vins);
                    basilisk_bobpayment_reclaim(myinfo,swap,INSTANTDEX_LOCKTIME);
                    break;
                }
            }
        }
    }
    else
    {
        swap->bobdeposit.spendlen = basilisk_bobscript(swap->bobdeposit.rmd160,swap->bobdeposit.redeemscript,&swap->bobdeposit.redeemlen,swap->bobdeposit.spendscript,0,&swap->bobdeposit.locktime,&swap->bobdeposit.secretstart,swap,1);
        if ( genflag != 0 )
        {
            for (i=0; i<3; i++)
            {
                basilisk_rawtx_gen("deposit",myinfo,1,1,&swap->bobdeposit,swap->bobdeposit.locktime,swap->bobdeposit.spendscript,swap->bobdeposit.spendlen,swap->bobdeposit.coin->chain->txfee,1);
                if ( swap->bobdeposit.txbytes == 0 || swap->bobdeposit.spendlen == 0 )
                {
                    printf("error bob generating %p deposit.%d\n",swap->bobdeposit.txbytes,swap->bobdeposit.spendlen);
                    sleep(3);
                }
                else
                {
                    for (j=0; j<swap->bobdeposit.datalen; j++)
                        printf("%02x",swap->bobdeposit.txbytes[j]);
                    printf(" <- bobdeposit.%d\n",swap->bobdeposit.datalen);
                    for (j=0; j<swap->bobdeposit.redeemlen; j++)
                        printf("%02x",swap->bobdeposit.redeemscript[j]);
                    printf(" <- redeem.%d\n",swap->bobdeposit.redeemlen);
                    printf("GENERATED BOB DEPOSIT.(%s)\n",bits256_str(str,swap->bobdeposit.txid));
                    iguana_unspents_mark(myinfo,swap->bobcoin,swap->bobdeposit.vins);
                    basilisk_bobdeposit_refund(myinfo,swap,INSTANTDEX_LOCKTIME);
                    break;
                }
            }
        }
        //for (i=0; i<swap->bobdeposit.redeemlen; i++)
        //    printf("%02x",swap->bobdeposit.redeemscript[i]);
        //printf(" <- bobdeposit.%d\n",i);
    }
}

int32_t basilisk_verify_privi(struct supernet_info *myinfo,void *ptr,uint8_t *data,int32_t datalen)
{
    int32_t j,wrongfirstbyte,len = 0; bits256 privkey,pubi; char str[65],str2[65]; uint8_t secret160[20],pubkey33[33]; uint64_t txid; struct basilisk_swap *swap = ptr;
    if ( datalen == sizeof(bits256) )
    {
        for (j=0; j<32; j++)
            privkey.bytes[j] = data[len++];
        revcalc_rmd160_sha256(secret160,privkey);
        memcpy(&txid,secret160,sizeof(txid));
        pubi = bitcoin_pubkey33(myinfo->ctx,pubkey33,privkey);
        if ( basilisk_verify_pubpair(&wrongfirstbyte,swap,swap->choosei,pubkey33[0],pubi,txid) == 0 )
        {
            if ( swap->iambob != 0 )
            {
                swap->privAm = privkey;
                vcalc_sha256(0,swap->secretAm256,privkey.bytes,sizeof(privkey));
                printf("set privAm.%s %s\n",bits256_str(str,swap->privAm),bits256_str(str2,*(bits256 *)swap->secretAm256));
                basilisk_bobscripts_set(myinfo,swap,0,1);
            }
            else
            {
                swap->privBn = privkey;
                vcalc_sha256(0,swap->secretBn256,privkey.bytes,sizeof(privkey));
                printf("set privBn.%s %s\n",bits256_str(str,swap->privBn),bits256_str(str2,*(bits256 *)swap->secretBn256));
            }
            char str[65]; printf("privi verified.(%s)\n",bits256_str(str,privkey));
            return(0);
        }
    }
    return(-1);
}

uint32_t basilisk_swaprecv(struct supernet_info *myinfo,uint8_t *verifybuf,int32_t maxlen,int32_t *datalenp,bits256 srchash,bits256 desthash,uint32_t channel,uint32_t msgbits)
{
    cJSON *retarray,*obj,*item,*msgarray; char *hexstr,*keystr,*retstr; uint32_t rawcrcs[64],crc=0; int32_t numcrcs=0,i,j,m,n,datalen,datalens[64];; uint8_t key[BASILISK_KEYSIZE];
    *datalenp = 0;
    memset(rawcrcs,0,sizeof(rawcrcs));
    memset(datalens,0,sizeof(datalens));
    if ( (retarray= basilisk_channelget(myinfo,srchash,desthash,channel,msgbits,0)) != 0 )
    {
        //printf("retarray.(%s)\n",jprint(retarray,0));
        if ( (n= cJSON_GetArraySize(retarray)) > 0 )
        {
            for (i=0; i<n; i++)
            {
                obj = jitem(retarray,i);
                if ( jobj(obj,"error") != 0 )
                    continue;
                if ( (msgarray= jarray(&m,obj,"messages")) != 0 )
                {
                    for (j=0; j<m; j++)
                    {
                        item = jitem(msgarray,j);
                        keystr = hexstr = 0;
                        datalen = 0;
                        if ( (keystr= jstr(item,"key")) != 0 && is_hexstr(keystr,0) == BASILISK_KEYSIZE*2 && (hexstr= jstr(item,"data")) != 0 && (datalen= is_hexstr(hexstr,0)) > 0 )
                        {
                            decode_hex(key,BASILISK_KEYSIZE,keystr);
                            datalen >>= 1;
                            if ( datalen < maxlen )
                            {
                                decode_hex(verifybuf,datalen,hexstr);
                                if ( (retstr= basilisk_respond_addmessage(myinfo,key,BASILISK_KEYSIZE,verifybuf,datalen,juint(item,"expiration"),juint(item,"duration"))) != 0 )
                                {
                                    if ( numcrcs < sizeof(rawcrcs)/sizeof(*rawcrcs) )
                                    {
                                        rawcrcs[numcrcs] = calc_crc32(0,verifybuf,datalen);
                                        datalens[numcrcs] = datalen;
                                        numcrcs++;
                                    }
                                    free(retstr);
                                }
                            } else printf("datalen.%d >= maxlen.%d\n",datalen,maxlen);
                        } else printf("not keystr.%p or no data.%p or bad datalen.%d\n",keystr,hexstr,datalen);
                    }
                }
                //printf("(%s).%d ",jprint(item,0),i);
            }
            //printf("n.%d maxlen.%d\n",n,maxlen);
        }
        free_json(retarray);
        if ( (crc= basilisk_majority32(datalenp,rawcrcs,datalens,numcrcs)) != 0 )
        {
            //printf("have majority crc.%08x\n",crc);
        }
        //else printf("no majority from rawcrcs.%d\n",numcrcs);
    }
    return(crc);
}

int32_t basilisk_process_swapverify(struct supernet_info *myinfo,void *ptr,int32_t (*internal_func)(struct supernet_info *myinfo,void *ptr,uint8_t *data,int32_t datalen),uint32_t channel,uint32_t msgid,uint8_t *data,int32_t datalen,uint32_t expiration,uint32_t duration)
{
    struct basilisk_swap *swap = ptr;
    if ( internal_func != 0 )
        return((*internal_func)(myinfo,swap,data,datalen));
    else return(0);
}

int32_t basilisk_swapget(struct supernet_info *myinfo,struct basilisk_swap *swap,uint32_t msgbits,uint8_t *data,int32_t maxlen,int32_t (*basilisk_verify_func)(struct supernet_info *myinfo,void *ptr,uint8_t *data,int32_t datalen))
{
    int32_t datalen; uint32_t crc;
    if ( (crc= basilisk_swaprecv(myinfo,swap->verifybuf,sizeof(swap->verifybuf),&datalen,swap->otherhash,swap->myhash,swap->req.quoteid,msgbits)) != 0 )
    {
        if ( datalen > 0 && datalen < maxlen )
        {
            memcpy(data,swap->verifybuf,datalen);
            return((*basilisk_verify_func)(myinfo,swap,data,datalen));
        }
    }
    return(-1);
}

uint32_t basilisk_swapcrcsend(struct supernet_info *myinfo,uint8_t *verifybuf,int32_t maxlen,bits256 srchash,bits256 desthash,uint32_t channel,uint32_t msgbits,uint8_t *data,int32_t datalen,uint32_t crcs[2])
{
    uint32_t crc; int32_t recvlen;
    if ( crcs != 0 )
    {
        crc = calc_crc32(0,data,datalen);
        if ( crcs[0] != crc )
            crcs[0] = crc, crcs[1] = 0;
        else
        {
            if ( crcs[1] == 0 )
                crcs[1] = basilisk_swaprecv(myinfo,verifybuf,maxlen,&recvlen,srchash,desthash,channel,msgbits);
            if ( crcs[0] == crcs[1] && datalen == recvlen )
                return(crcs[0]);
        }
    }
    return(0);
}

uint32_t basilisk_swapsend(struct supernet_info *myinfo,struct basilisk_swap *swap,uint32_t msgbits,uint8_t *data,int32_t datalen,uint32_t nextbits,uint32_t crcs[2])
{
    if ( basilisk_swapcrcsend(myinfo,swap->verifybuf,sizeof(swap->verifybuf),swap->myhash,swap->otherhash,swap->req.quoteid,msgbits,data,datalen,crcs) != 0 )
        return(nextbits);
    if ( basilisk_channelsend(myinfo,swap->myhash,swap->otherhash,swap->req.quoteid,msgbits,data,datalen,INSTANTDEX_LOCKTIME*2) == 0 )
        return(nextbits);
    printf("ERROR basilisk_channelsend\n");
    return(0);
}

int32_t basilisk_priviextract(struct supernet_info *myinfo,struct iguana_info *coin,char *name,bits256 *destp,uint8_t secret160[20],bits256 srctxid,int32_t srcvout)
{
    bits256 txid,privkey; char str[65]; int32_t i,vini,scriptlen; uint8_t rmd160[20],scriptsig[IGUANA_MAXSCRIPTSIZE];
    if ( (vini= iguana_vinifind(myinfo,coin,&txid,srctxid,srcvout)) >= 0 )
    {
        if ( (scriptlen= iguana_scriptsigextract(myinfo,coin,scriptsig,sizeof(scriptsig),txid,vini)) > 0 )
        {
            for (i=0; i<32; i++)
                privkey.bytes[i] = scriptsig[scriptlen - 33 + i];
            revcalc_rmd160_sha256(rmd160,privkey);
            if ( memcmp(secret160,rmd160,sizeof(rmd160)) == sizeof(rmd160) )
            {
                *destp = privkey;
                printf("found %s (%s)\n",name,bits256_str(str,privkey));
                return(0);
            }
        }
    }
    return(-1);
}

int32_t basilisk_privBn_extract(struct supernet_info *myinfo,struct basilisk_swap *swap,uint8_t *data,int32_t maxlen)
{
    if ( basilisk_priviextract(myinfo,swap->bobcoin,"privBn",&swap->privBn,swap->secretBn,swap->bobrefund.actualtxid,0) == 0 )
    {
        
    }
    if ( basilisk_swapget(myinfo,swap,0x40000000,data,maxlen,basilisk_verify_privi) == 0 )
    {
        if ( bits256_nonz(swap->privBn) != 0 && swap->alicereclaim.txbytes == 0 )
        {
            char str[65]; printf("have privBn.%s\n",bits256_str(str,swap->privBn));
            return(basilisk_alicepayment_spend(myinfo,swap,&swap->alicereclaim));
        }
    }
    return(-1);
}

int32_t basilisk_privAm_extract(struct supernet_info *myinfo,struct basilisk_swap *swap)
{
    if ( basilisk_priviextract(myinfo,swap->bobcoin,"privAm",&swap->privAm,swap->secretAm,swap->bobpayment.actualtxid,0) == 0 )
    {
        
    }
    if ( bits256_nonz(swap->privAm) != 0 && swap->bobspend.txbytes == 0 )
    {
        char str[65]; printf("have privAm.%s\n",bits256_str(str,swap->privAm));
        return(basilisk_alicepayment_spend(myinfo,swap,&swap->bobspend));
    }
    return(-1);
}

bits256 instantdex_derivekeypair(struct supernet_info *myinfo,bits256 *newprivp,uint8_t pubkey[33],bits256 privkey,bits256 orderhash)
{
    bits256 sharedsecret;
    sharedsecret = curve25519_shared(privkey,orderhash);
    vcalc_sha256cat(newprivp->bytes,orderhash.bytes,sizeof(orderhash),sharedsecret.bytes,sizeof(sharedsecret));
    return(bitcoin_pubkey33(myinfo->ctx,pubkey,*newprivp));
}

int32_t instantdex_pubkeyargs(struct supernet_info *myinfo,struct basilisk_swap *swap,int32_t numpubs,bits256 privkey,bits256 hash,int32_t firstbyte)
{
    char buf[3]; int32_t i,n,m,len=0; bits256 pubi,reveal; uint64_t txid; uint8_t secret160[20],pubkey[33];
    sprintf(buf,"%c0",'A' - 0x02 + firstbyte);
    if ( numpubs > 2 )
    {
        if ( swap->numpubs+2 >= numpubs )
            return(numpubs);
        printf(">>>>>> start generating %s\n",buf);
    }
    for (i=n=m=0; i<numpubs*100 && n<numpubs; i++)
    {
        pubi = instantdex_derivekeypair(myinfo,&privkey,pubkey,privkey,hash);
        //printf("i.%d n.%d numpubs.%d %02x vs %02x\n",i,n,numpubs,pubkey[0],firstbyte);
        if ( pubkey[0] != firstbyte )
            continue;
        if ( n < 2 )
        {
            if ( bits256_nonz(swap->mypubs[n]) == 0 )
            {
                swap->myprivs[n] = privkey;
                memcpy(swap->mypubs[n].bytes,pubkey+1,sizeof(bits256));
                reveal = basilisk_revealkey(privkey,swap->mypubs[n]);
                if ( swap->iambob != 0 )
                {
                    if ( n == 0 )
                        swap->pubB0 = reveal;
                    else if ( n == 1 )
                        swap->pubB1 = reveal;
                }
                else if ( swap->iambob == 0 )
                {
                    if ( n == 0 )
                        swap->pubA0 = reveal;
                    else if ( n == 1 )
                        swap->pubA1 = reveal;
                }
            }
        }
        if ( m < INSTANTDEX_DECKSIZE )
        {
            swap->privkeys[m] = privkey;
            revcalc_rmd160_sha256(secret160,privkey);//.bytes,sizeof(privkey));
            memcpy(&txid,secret160,sizeof(txid));
            len += iguana_rwnum(1,(uint8_t *)&swap->deck[m][0],sizeof(txid),&txid);
            len += iguana_rwnum(1,(uint8_t *)&swap->deck[m][1],sizeof(pubi.txid),&pubi.txid);
            m++;
            if ( m > swap->numpubs )
                swap->numpubs = m;
        }
        n++;
    }
    if ( n > 2 || m > 2 )
        printf("n.%d m.%d len.%d numpubs.%d\n",n,m,len,swap->numpubs);
    return(n);
}

void basilisk_rawtx_setparms(char *name,struct supernet_info *myinfo,struct basilisk_swap *swap,struct basilisk_rawtx *rawtx,struct iguana_info *coin,int32_t numconfirms,int32_t vintype,uint64_t satoshis,int32_t vouttype,uint8_t *pubkey33)
{
    strcpy(rawtx->name,name);
    rawtx->coin = coin;
    rawtx->numconfirms = numconfirms;
    if ( (rawtx->amount= satoshis) < 10000 )
        rawtx->amount = 10000;
    rawtx->vintype = vintype; // 0 -> std, 2 -> 2of2, 3 -> spend bobpayment, 4 -> spend bobdeposit
    rawtx->vouttype = vouttype; // 0 -> fee, 1 -> std, 2 -> 2of2, 3 -> bobpayment, 4 -> bobdeposit
    if ( rawtx->vouttype == 0 )
    {
        if ( strcmp(coin->symbol,"BTC") == 0 && (swap->req.quoteid % 10) == 0 )
            decode_hex(rawtx->rmd160,20,TIERNOLAN_RMD160);
        else decode_hex(rawtx->rmd160,20,INSTANTDEX_RMD160);
        bitcoin_address(rawtx->destaddr,rawtx->coin->chain->pubtype,rawtx->rmd160,20);
    }
    if ( pubkey33 != 0 )
    {
        memcpy(rawtx->pubkey33,pubkey33,33);
        bitcoin_address(rawtx->destaddr,rawtx->coin->chain->pubtype,rawtx->pubkey33,33);
        bitcoin_addr2rmd160(&rawtx->addrtype,rawtx->rmd160,rawtx->destaddr);
    }
    if ( rawtx->vouttype <= 1 && rawtx->destaddr[0] != 0 )
    {
        rawtx->spendlen = bitcoin_standardspend(rawtx->spendscript,0,rawtx->rmd160);
        printf("%s spendlen.%d %s <- %.8f\n",name,rawtx->spendlen,rawtx->destaddr,dstr(rawtx->amount));
    } else printf("%s vouttype.%d destaddr.(%s)\n",name,rawtx->vouttype,rawtx->destaddr);
}

struct basilisk_swap *bitcoin_swapinit(struct supernet_info *myinfo,struct basilisk_swap *swap)
{
    struct iguana_info *coin; uint8_t *alicepub33=0,*bobpub33=0; int32_t x = -1;
    if ( strcmp("BTC",swap->req.src) == 0 )
    {
        swap->bobcoin = iguana_coinfind("BTC");
        swap->bobsatoshis = swap->req.srcamount;
        swap->bobconfirms = (1 + sqrt(dstr(swap->bobsatoshis) * .1));
        swap->alicecoin = iguana_coinfind(swap->req.dest);
        swap->alicesatoshis = swap->req.destamount;
        swap->aliceconfirms = swap->bobconfirms * 3;
    }
    else if ( strcmp("BTC",swap->req.dest) == 0 )
    {
        swap->bobcoin = iguana_coinfind("BTC");
        swap->bobsatoshis = swap->req.destamount;
        swap->bobconfirms = (1 + sqrt(dstr(swap->bobsatoshis) * .1));
        swap->alicecoin = iguana_coinfind(swap->req.src);
        swap->alicesatoshis = swap->req.srcamount;
        swap->aliceconfirms = swap->bobconfirms * 3;
    }
    else
    {
        if ( (coin= iguana_coinfind(swap->req.src)) != 0 )
        {
            if ( coin->chain->havecltv != 0 )
            {
                swap->bobcoin = coin;
                swap->bobsatoshis = swap->req.srcamount;
                swap->alicecoin = iguana_coinfind(swap->req.dest);
                swap->alicesatoshis = swap->req.destamount;
            }
            else if ( (coin= iguana_coinfind(swap->req.dest)) != 0 )
            {
                if ( coin->chain->havecltv != 0 )
                {
                    swap->bobcoin = coin;
                    swap->bobsatoshis = swap->req.destamount;
                    swap->alicecoin = iguana_coinfind(swap->req.src);
                    swap->alicesatoshis = swap->req.srcamount;
                }
            }
        }
    }
    if ( swap->bobcoin == 0 || swap->alicecoin == 0 )
    {
        printf("missing BTC.%p or missing alicecoin.%p\n",swap->bobcoin,swap->alicecoin);
        free(swap);
        return(0);
    }
    if ( swap->bobconfirms == 0 )
        swap->bobconfirms = swap->bobcoin->chain->minconfirms;
    if ( swap->aliceconfirms == 0 )
        swap->aliceconfirms = swap->alicecoin->chain->minconfirms;
    if ( (swap->bobinsurance= (swap->bobsatoshis / INSTANTDEX_INSURANCEDIV)) < 10000 )
        swap->bobinsurance = 10000;
    if ( (swap->aliceinsurance= (swap->alicesatoshis / INSTANTDEX_INSURANCEDIV)) < 10000 )
        swap->aliceinsurance = 10000;
    strcpy(swap->bobstr,swap->bobcoin->symbol);
    strcpy(swap->alicestr,swap->alicecoin->symbol);
    swap->started = (uint32_t)time(NULL);
    swap->expiration = swap->req.timestamp + INSTANTDEX_LOCKTIME*2;
    swap->locktime = swap->req.timestamp + INSTANTDEX_LOCKTIME;
    OS_randombytes((uint8_t *)&swap->choosei,sizeof(swap->choosei));
    if ( swap->choosei < 0 )
        swap->choosei = -swap->choosei;
    swap->choosei %= INSTANTDEX_DECKSIZE;
    swap->otherchoosei = -1;
    swap->myhash = myinfo->myaddr.persistent;
    if ( bits256_cmp(swap->myhash,swap->req.srchash) == 0 )
    {
        swap->otherhash = swap->req.desthash;
        if ( strcmp(swap->req.src,swap->bobstr) == 0 )
            swap->iambob = 1;
        else if ( strcmp(swap->req.dest,swap->alicestr) == 0 )
        {
            printf("neither bob nor alice error\n");
            return(0);
        }
    }
    else if ( bits256_cmp(swap->myhash,swap->req.desthash) == 0 )
    {
        swap->otherhash = swap->req.srchash;
        if ( strcmp(swap->req.dest,swap->bobstr) == 0 )
            swap->iambob = 1;
        else if ( strcmp(swap->req.src,swap->alicestr) != 0 )
        {
            printf("neither alice nor bob error\n");
            return(0);
        }
    }
    else
    {
        printf("neither src nor dest error\n");
        return(0);
    }
    if ( bits256_nonz(myinfo->persistent_priv) == 0 || (x= instantdex_pubkeyargs(myinfo,swap,2 + INSTANTDEX_DECKSIZE,myinfo->persistent_priv,swap->orderhash,0x02+swap->iambob)) != 2 + INSTANTDEX_DECKSIZE )
    {
        printf("couldnt generate privkeys %d\n",x);
        return(0);
    }
    if ( swap->iambob != 0 )
    {
        basilisk_rawtx_setparms("myfee",myinfo,swap,&swap->myfee,swap->bobcoin,0,0,swap->bobsatoshis/INSTANTDEX_DECKSIZE,0,0);
        basilisk_rawtx_setparms("otherfee",myinfo,swap,&swap->otherfee,swap->alicecoin,0,0,swap->alicesatoshis/INSTANTDEX_DECKSIZE,0,0);
        bobpub33 = myinfo->persistent_pubkey33;
    }
    else
    {
        basilisk_rawtx_setparms("otherfee",myinfo,swap,&swap->otherfee,swap->bobcoin,0,0,swap->bobsatoshis/INSTANTDEX_DECKSIZE,0,0);
        basilisk_rawtx_setparms("myfee",myinfo,swap,&swap->myfee,swap->alicecoin,0,0,swap->alicesatoshis/INSTANTDEX_DECKSIZE,0,0);
        alicepub33 = myinfo->persistent_pubkey33;
    }
    basilisk_rawtx_setparms("bobdeposit",myinfo,swap,&swap->bobdeposit,swap->bobcoin,swap->bobconfirms,0,swap->bobsatoshis*1.1,4,0);
    basilisk_rawtx_setparms("bobrefund",myinfo,swap,&swap->bobrefund,swap->bobcoin,1,4,swap->bobsatoshis*1.1-swap->bobcoin->txfee,1,bobpub33);
    swap->bobrefund.suppress_pubkeys = 1;
    basilisk_rawtx_setparms("aliceclaim",myinfo,swap,&swap->aliceclaim,swap->bobcoin,1,4,swap->bobsatoshis*1.1-swap->bobcoin->txfee,1,alicepub33);
    swap->aliceclaim.suppress_pubkeys = 1;

    basilisk_rawtx_setparms("bobpayment",myinfo,swap,&swap->bobpayment,swap->bobcoin,swap->bobconfirms,0,swap->bobsatoshis,3,0);
    basilisk_rawtx_setparms("alicespend",myinfo,swap,&swap->alicespend,swap->bobcoin,swap->bobconfirms,3,swap->bobsatoshis - swap->bobcoin->txfee,1,alicepub33);
    swap->alicespend.suppress_pubkeys = 1;
    basilisk_rawtx_setparms("bobreclaim",myinfo,swap,&swap->bobreclaim,swap->bobcoin,swap->bobconfirms,3,swap->bobsatoshis - swap->bobcoin->txfee,1,bobpub33);
    swap->bobreclaim.suppress_pubkeys = 1;

    basilisk_rawtx_setparms("alicepayment",myinfo,swap,&swap->alicepayment,swap->alicecoin,swap->aliceconfirms,0,swap->alicesatoshis,2,0);
    basilisk_rawtx_setparms("bobspend",myinfo,swap,&swap->bobspend,swap->alicecoin,swap->aliceconfirms,2,swap->alicesatoshis-swap->alicecoin->txfee,1,bobpub33);
    swap->bobspend.suppress_pubkeys = 1;
    basilisk_rawtx_setparms("alicereclaim",myinfo,swap,&swap->alicereclaim,swap->alicecoin,swap->aliceconfirms,2,swap->alicesatoshis-swap->alicecoin->txfee,1,alicepub33);
    swap->alicereclaim.suppress_pubkeys = 1;
    printf("IAMBOB.%d\n",swap->iambob);
    return(swap);
}
// end of alice/bob code

void basilisk_rawtx_purge(struct basilisk_rawtx *rawtx)
{
    if ( rawtx->txbytes != 0 )
        free(rawtx->txbytes), rawtx->txbytes = 0;
}

void basilisk_swap_finished(struct supernet_info *myinfo,struct basilisk_swap *swap)
{
    swap->finished = (uint32_t)time(NULL);
    // save to permanent storage
    basilisk_rawtx_purge(&swap->bobdeposit);
    basilisk_rawtx_purge(&swap->bobpayment);
    basilisk_rawtx_purge(&swap->alicepayment);
    basilisk_rawtx_purge(&swap->myfee);
    basilisk_rawtx_purge(&swap->otherfee);
    basilisk_rawtx_purge(&swap->alicereclaim);
    basilisk_rawtx_purge(&swap->alicespend);
    basilisk_rawtx_purge(&swap->bobreclaim);
    basilisk_rawtx_purge(&swap->bobspend);
    basilisk_rawtx_purge(&swap->bobrefund);
}

void basilisk_swap_purge(struct supernet_info *myinfo,struct basilisk_swap *swap)
{
    int32_t i,n;
    // while still in orderbook, wait
    return;
    portable_mutex_lock(&myinfo->DEX_swapmutex);
    n = myinfo->numswaps;
    for (i=0; i<n; i++)
        if ( myinfo->swaps[i] == swap )
        {
            myinfo->swaps[i] = myinfo->swaps[--myinfo->numswaps];
            myinfo->swaps[myinfo->numswaps] = 0;
            basilisk_swap_finished(myinfo,swap);
            break;
        }
    portable_mutex_unlock(&myinfo->DEX_swapmutex);
}

int32_t basilisk_verify_otherstatebits(struct supernet_info *myinfo,void *ptr,uint8_t *data,int32_t datalen)
{
    struct basilisk_swap *swap = ptr;
    if ( datalen == sizeof(swap->otherstatebits) )
        return(iguana_rwnum(0,data,sizeof(swap->otherstatebits),&swap->otherstatebits));
    else return(-1);
}
               
int32_t basilisk_verify_choosei(struct supernet_info *myinfo,void *ptr,uint8_t *data,int32_t datalen)
{
    int32_t otherchoosei=-1,i,len = 0; struct basilisk_swap *swap = ptr;
    if ( datalen == sizeof(otherchoosei)+sizeof(bits256)*2 )
    {
        len += iguana_rwnum(0,data,sizeof(otherchoosei),&otherchoosei);
        if ( otherchoosei >= 0 && otherchoosei < INSTANTDEX_DECKSIZE )
        {
            printf("otherchoosei.%d\n",otherchoosei);
            swap->otherchoosei = otherchoosei;
            if ( swap->iambob != 0 )
            {
                for (i=0; i<32; i++)
                    swap->pubA0.bytes[i] = data[len++];
                for (i=0; i<32; i++)
                    swap->pubA1.bytes[i] = data[len++];
                char str[65]; printf("GOT pubA0/1 %s\n",bits256_str(str,swap->pubA0));
            }
            else
            {
                for (i=0; i<32; i++)
                    swap->pubB0.bytes[i] = data[len++];
                for (i=0; i<32; i++)
                    swap->pubB1.bytes[i] = data[len++];
            }
            return(0);
        }
    }
    printf("illegal otherchoosei.%d datalen.%d vs %d\n",otherchoosei,datalen,(int32_t)(sizeof(otherchoosei)+sizeof(bits256)*2));
    return(-1);
}

int32_t basilisk_swapdata_deck(struct supernet_info *myinfo,struct basilisk_swap *swap,uint8_t *data,int32_t maxlen)
{
    int32_t i,datalen = 0;
    for (i=0; i<sizeof(swap->deck)/sizeof(swap->deck[0][0]); i++)
        datalen += iguana_rwnum(1,&data[datalen],sizeof(swap->deck[i>>1][i&1]),&swap->deck[i>>1][i&1]);
    return(datalen);
}

int32_t basilisk_verify_otherdeck(struct supernet_info *myinfo,void *ptr,uint8_t *data,int32_t datalen)
{
    int32_t i,len = 0; struct basilisk_swap *swap = ptr;
    for (i=0; i<sizeof(swap->otherdeck)/sizeof(swap->otherdeck[0][0]); i++)
        len += iguana_rwnum(0,&data[len],sizeof(swap->otherdeck[i>>1][i&1]),&swap->otherdeck[i>>1][i&1]);
    return(0);
}

int32_t basilisk_verify_privkeys(struct supernet_info *myinfo,void *ptr,uint8_t *data,int32_t datalen)
{
    int32_t i,j,wrongfirstbyte=0,errs=0,len = 0; bits256 otherpriv,pubi; uint8_t secret160[20],otherpubkey[33]; uint64_t txid; struct basilisk_swap *swap = ptr;
    printf("verify privkeys choosei.%d otherchoosei.%d datalen.%d vs %d\n",swap->choosei,swap->otherchoosei,datalen,(int32_t)sizeof(swap->privkeys)+20+32);
    if ( swap->cutverified == 0 && swap->otherchoosei >= 0 && datalen == sizeof(swap->privkeys)+20+2*32 )
    {
        for (i=errs=0; i<sizeof(swap->privkeys)/sizeof(*swap->privkeys); i++)
        {
            for (j=0; j<32; j++)
                otherpriv.bytes[j] = data[len++];
            if ( i != swap->choosei )
            {
                pubi = bitcoin_pubkey33(myinfo->ctx,otherpubkey,otherpriv);
                revcalc_rmd160_sha256(secret160,otherpriv);//.bytes,sizeof(otherpriv));
                memcpy(&txid,secret160,sizeof(txid));
                errs += basilisk_verify_pubpair(&wrongfirstbyte,swap,i,otherpubkey[0],pubi,txid);
            }
        }
        if ( errs == 0 && wrongfirstbyte == 0 )
        {
            swap->cutverified = 1, printf("CUT VERIFIED\n");
            if ( swap->iambob != 0 )
            {
                for (i=0; i<32; i++)
                    swap->pubAm.bytes[i] = data[len++];
                for (i=0; i<20; i++)
                    swap->secretAm[i] = data[len++];
                for (i=0; i<32; i++)
                    swap->secretAm256[i] = data[len++];
                basilisk_bobscripts_set(myinfo,swap,1,1);
            }
            else
            {
                for (i=0; i<32; i++)
                    swap->pubBn.bytes[i] = data[len++];
                for (i=0; i<20; i++)
                    swap->secretBn[i] = data[len++];
                for (i=0; i<32; i++)
                    swap->secretBn256[i] = data[len++];
                //basilisk_bobscripts_set(myinfo,swap,0);
            }
        } else printf("failed verification: wrong firstbyte.%d errs.%d\n",wrongfirstbyte,errs);
    }
    printf("privkeys errs.%d wrongfirstbyte.%d\n",errs,wrongfirstbyte);
    return(errs);
}

uint32_t basilisk_swapdata_rawtxsend(struct supernet_info *myinfo,struct basilisk_swap *swap,uint32_t msgbits,uint8_t *data,int32_t maxlen,struct basilisk_rawtx *rawtx,uint32_t nextbits)
{
    uint8_t sendbuf[32768]; int32_t sendlen;
    if ( basilisk_swapdata_rawtx(myinfo,swap,data,maxlen,rawtx) != 0 )
    {
        if ( bits256_nonz(rawtx->signedtxid) != 0 )//&& bits256_nonz(rawtx->actualtxid) == 0 )
        {
            char str[65],str2[65];
            rawtx->actualtxid = basilisk_swap_broadcast(rawtx->name,myinfo,swap,rawtx->coin,rawtx->txbytes,rawtx->datalen);
            if ( bits256_nonz(rawtx->actualtxid) == 0 )
                printf("%s rawtxsend %s vs %s\n",rawtx->name,bits256_str(str,rawtx->signedtxid),bits256_str(str2,rawtx->actualtxid));
            if ( bits256_nonz(rawtx->actualtxid) != 0 && msgbits != 0 )
            {
                sendlen = 0;
                sendbuf[sendlen++] = rawtx->datalen & 0xff;
                sendbuf[sendlen++] = (rawtx->datalen >> 8) & 0xff;
                sendbuf[sendlen++] = rawtx->redeemlen;
                memcpy(&sendbuf[sendlen],rawtx->txbytes,rawtx->datalen), sendlen += rawtx->datalen;
                if ( rawtx->redeemlen > 0 && rawtx->redeemlen < 0x100 )
                {
                    memcpy(&sendbuf[sendlen],rawtx->redeemscript,rawtx->redeemlen);
                    sendlen += rawtx->redeemlen;
                }
                printf("sendlen.%d datalen.%d redeemlen.%d\n",sendlen,rawtx->datalen,rawtx->redeemlen);
                return(basilisk_swapsend(myinfo,swap,msgbits,sendbuf,sendlen,nextbits,rawtx->crcs));
            }
        }
        return(nextbits);
    } else printf("error from basilisk_swapdata_rawtx %p len.%d\n",rawtx->txbytes,rawtx->datalen);
    return(0);
}

void basilisk_sendpubkeys(struct supernet_info *myinfo,struct basilisk_swap *swap,uint8_t *data,int32_t maxlen)
{
    int32_t datalen;
    datalen = basilisk_swapdata_deck(myinfo,swap,data,maxlen);
    //printf("send deck.%d\n",datalen);
    swap->statebits |= basilisk_swapsend(myinfo,swap,0x02,data,datalen,0x01,swap->crcs_mypub);
}

int32_t basilisk_checkdeck(struct supernet_info *myinfo,struct basilisk_swap *swap,uint8_t *data,int32_t maxlen)
{
    if ( (swap->statebits & 0x02) == 0 )
    {
        //printf("check for other deck\n");
        if ( basilisk_swapget(myinfo,swap,0x02,data,maxlen,basilisk_verify_otherdeck) == 0 )
            swap->statebits |= 0x02;
        else return(-1);
    }
    return(0);
}

void basilisk_sendstate(struct supernet_info *myinfo,struct basilisk_swap *swap,uint8_t *data,int32_t maxlen)
{
    int32_t datalen;
    datalen = iguana_rwnum(1,data,sizeof(swap->statebits),&swap->statebits);
    basilisk_swapsend(myinfo,swap,0x80000000,data,datalen,0,0);
}

void basilisk_sendchoosei(struct supernet_info *myinfo,struct basilisk_swap *swap,uint8_t *data,int32_t maxlen)
{
    int32_t i,datalen; char str[65];
    datalen = iguana_rwnum(1,data,sizeof(swap->choosei),&swap->choosei);
    if ( swap->iambob != 0 )
    {
        for (i=0; i<32; i++)
            data[datalen++] = swap->pubB0.bytes[i];
        for (i=0; i<32; i++)
            data[datalen++] = swap->pubB1.bytes[i];
        printf("SEND pubB0/1 %s\n",bits256_str(str,swap->pubB0));
    }
    else
    {
        for (i=0; i<32; i++)
            data[datalen++] = swap->pubA0.bytes[i];
        for (i=0; i<32; i++)
            data[datalen++] = swap->pubA1.bytes[i];
        printf("SEND pubA0/1 %s\n",bits256_str(str,swap->pubA0));
    }
    swap->statebits |= basilisk_swapsend(myinfo,swap,0x08,data,datalen,0x04,swap->crcs_mychoosei);
}

void basilisk_waitchoosei(struct supernet_info *myinfo,struct basilisk_swap *swap,uint8_t *data,int32_t maxlen)
{
    uint8_t pubkey33[33]; char str[65],str2[65];
    //printf("check otherchoosei\n");
    if ( basilisk_swapget(myinfo,swap,0x08,data,maxlen,basilisk_verify_choosei) == 0 )
    {
        if ( swap->iambob != 0 )
        {
            if ( bits256_nonz(swap->privBn) == 0 )
            {
                swap->privBn = swap->privkeys[swap->otherchoosei];
                memset(&swap->privkeys[swap->otherchoosei],0,sizeof(swap->privkeys[swap->otherchoosei]));
                revcalc_rmd160_sha256(swap->secretBn,swap->privBn);//.bytes,sizeof(swap->privBn));
                vcalc_sha256(0,swap->secretBn256,swap->privBn.bytes,sizeof(swap->privBn));
                swap->pubBn = bitcoin_pubkey33(myinfo->ctx,pubkey33,swap->privBn);
                printf("set privBn.%s %s\n",bits256_str(str,swap->privBn),bits256_str(str2,*(bits256 *)swap->secretBn256));
                basilisk_bobscripts_set(myinfo,swap,1,1);
             }
        }
        else
        {
            if ( bits256_nonz(swap->privAm) == 0 )
            {
                swap->privAm = swap->privkeys[swap->otherchoosei];
                memset(&swap->privkeys[swap->otherchoosei],0,sizeof(swap->privkeys[swap->otherchoosei]));
                revcalc_rmd160_sha256(swap->secretAm,swap->privAm);//.bytes,sizeof(swap->privAm));
                vcalc_sha256(0,swap->secretAm256,swap->privAm.bytes,sizeof(swap->privAm));
                swap->pubAm = bitcoin_pubkey33(myinfo->ctx,pubkey33,swap->privAm);
                printf("set privAm.%s %s\n",bits256_str(str,swap->privAm),bits256_str(str2,*(bits256 *)swap->secretAm256));
                //basilisk_bobscripts_set(myinfo,swap,0);
            }
        }
        swap->statebits |= 0x08;
    }
}

void basilisk_sendmostprivs(struct supernet_info *myinfo,struct basilisk_swap *swap,uint8_t *data,int32_t maxlen)
{
    int32_t i,j,datalen;
    datalen = 0;
    for (i=0; i<sizeof(swap->privkeys)/sizeof(*swap->privkeys); i++)
    {
        for (j=0; j<32; j++)
            data[datalen++] = (i == swap->otherchoosei) ? 0 : swap->privkeys[i].bytes[j];
    }
    if ( swap->iambob != 0 )
    {
        for (i=0; i<32; i++)
            data[datalen++] = swap->pubBn.bytes[i];
        for (i=0; i<20; i++)
            data[datalen++] = swap->secretBn[i];
        for (i=0; i<32; i++)
            data[datalen++] = swap->secretBn256[i];
    }
    else
    {
        for (i=0; i<32; i++)
            data[datalen++] = swap->pubAm.bytes[i];
        for (i=0; i<20; i++)
            data[datalen++] = swap->secretAm[i];
        for (i=0; i<32; i++)
            data[datalen++] = swap->secretAm256[i];
    }
    //printf("send privkeys.%d\n",datalen);
    swap->statebits |= basilisk_swapsend(myinfo,swap,0x20,data,datalen,0x10,swap->crcs_myprivs);
}

void basilisk_alicepayment(struct supernet_info *myinfo,struct iguana_info *coin,struct basilisk_rawtx *alicepayment,bits256 pubAm,bits256 pubBn)
{
    alicepayment->spendlen = basilisk_alicescript(alicepayment->redeemscript,&alicepayment->redeemlen,alicepayment->spendscript,0,alicepayment->destaddr,coin->chain->p2shtype,pubAm,pubBn);
    basilisk_rawtx_gen("alicepayment",myinfo,0,1,alicepayment,alicepayment->locktime,alicepayment->spendscript,alicepayment->spendlen,coin->chain->txfee,1);
}

// detect insufficient funds/inputs
// mode to autocreate required outputs

void basilisk_swaploop(void *_swap)
{
    uint8_t *data; uint32_t expiration; int32_t retval=0,i,j,maxlen,datalen; struct supernet_info *myinfo; struct basilisk_swap *swap = _swap;
    myinfo = swap->myinfo;
    fprintf(stderr,"start swap\n");
    maxlen = 1024*1024 + sizeof(*swap);
    data = malloc(maxlen);
    expiration = (uint32_t)time(NULL) + 300;
    myinfo->DEXactive = expiration;
    while ( time(NULL) < expiration )
    {
        printf("A r%u/q%u swapstate.%x\n",swap->req.requestid,swap->req.quoteid,swap->statebits);
        basilisk_sendpubkeys(myinfo,swap,data,maxlen); // send pubkeys
        if ( basilisk_checkdeck(myinfo,swap,data,maxlen) == 0) // check for other deck 0x02
            basilisk_sendchoosei(myinfo,swap,data,maxlen);
        basilisk_waitchoosei(myinfo,swap,data,maxlen); // wait for choosei 0x08
        if ( (swap->statebits & (0x08|0x02)) == (0x08|0x02) )
            break;
        sleep(3);
    }
    while ( time(NULL) < expiration )
    {
        printf("B r%u/q%u swapstate.%x\n",swap->req.requestid,swap->req.quoteid,swap->statebits);
        basilisk_sendmostprivs(myinfo,swap,data,maxlen);
        if ( basilisk_swapget(myinfo,swap,0x20,data,maxlen,basilisk_verify_privkeys) == 0 )
        {
            swap->statebits |= 0x20;
            break;
        }
        sleep(3 + (swap->iambob == 0)*10);
    }
    if ( time(NULL) >= expiration )
        retval = -1;
    myinfo->DEXactive = swap->expiration;
    printf("C r%u/q%u swapstate.%x\n",swap->req.requestid,swap->req.quoteid,swap->statebits);
    if ( retval == 0 && (swap->statebits & 0x40) == 0 ) // send fee
    {
        basilisk_sendstate(myinfo,swap,data,maxlen);
        basilisk_swapget(myinfo,swap,0x80000000,data,maxlen,basilisk_verify_otherstatebits);
        if ( swap->myfee.txbytes == 0 )
        {
            for (i=0; i<20; i++)
                printf("%02x",swap->secretAm[i]);
            printf(" <- secretAm\n");
            for (i=0; i<32; i++)
                printf("%02x",swap->secretAm256[i]);
            printf(" <- secretAm256\n");
            for (i=0; i<32; i++)
                printf("%02x",swap->pubAm.bytes[i]);
            printf(" <- pubAm\n");
            for (i=0; i<20; i++)
                printf("%02x",swap->secretBn[i]);
            printf(" <- secretBn\n");
            for (i=0; i<32; i++)
                printf("%02x",swap->secretBn256[i]);
            printf(" <- secretBn256\n");
            for (i=0; i<32; i++)
                printf("%02x",swap->pubBn.bytes[i]);
            printf(" <- pubBn\n");
            for (i=0; i<32; i++)
                printf("%02x",swap->pubA0.bytes[i]);
            printf(" <- pubA0\n");
            for (i=0; i<32; i++)
                printf("%02x",swap->pubA1.bytes[i]);
            printf(" <- pubA1\n");
            for (i=0; i<32; i++)
                printf("%02x",swap->pubB0.bytes[i]);
            printf(" <- pubB0\n");
            for (i=0; i<32; i++)
                printf("%02x",swap->pubB1.bytes[i]);
            printf(" <- pubB1\n");
            if ( swap->iambob != 0 )
            {
                basilisk_bobscripts_set(myinfo,swap,1,1);
            }
            else
            {
                for (i=0; i<3; i++)
                {
                    basilisk_alicepayment(myinfo,swap->alicepayment.coin,&swap->alicepayment,swap->pubAm,swap->pubBn);
                    if ( swap->alicepayment.txbytes == 0 || swap->alicepayment.spendlen == 0 )
                    {
                        printf("error alice generating payment.%d\n",swap->alicepayment.spendlen);
                        retval = -4;
                        sleep(3);
                    }
                    else
                    {
                        retval = 0;
                        printf("ALICE PAYMENT created\n");
                        iguana_unspents_mark(myinfo,swap->alicecoin,swap->alicepayment.vins);
                        basilisk_txlog(myinfo,swap,&swap->alicepayment,-1);
                        break;
                    }
                }
            }
            if ( basilisk_rawtx_gen("myfee",myinfo,swap->iambob,1,&swap->myfee,0,swap->myfee.spendscript,swap->myfee.spendlen,swap->myfee.coin->chain->txfee,1) == 0 )
            {
                swap->statebits |= basilisk_swapdata_rawtxsend(myinfo,swap,0x80,data,maxlen,&swap->myfee,0x40);
                iguana_unspents_mark(myinfo,swap->iambob!=0?swap->bobcoin:swap->alicecoin,swap->myfee.vins);
                basilisk_txlog(myinfo,swap,&swap->myfee,-1);
            }
            else
            {
                printf("error creating myfee\n");
                retval = -6;
            }
        }
        basilisk_txlog(myinfo,swap,0,-1);
    }
    while ( retval == 0 && time(NULL) < swap->expiration )
    {
        printf("D r%u/q%u swapstate.%x otherstate.%x\n",swap->req.requestid,swap->req.quoteid,swap->statebits,swap->otherstatebits);
        if ( (swap->statebits & 0x80) == 0 ) // wait for fee
        {
            if ( basilisk_swapget(myinfo,swap,0x80,data,maxlen,basilisk_verify_otherfee) == 0 )
            {
                // verify and submit otherfee
                swap->statebits |= 0x80;
                basilisk_sendstate(myinfo,swap,data,maxlen);
            }
        }
        basilisk_sendstate(myinfo,swap,data,maxlen);
        basilisk_swapget(myinfo,swap,0x80000000,data,maxlen,basilisk_verify_otherstatebits);
        if ( (swap->otherstatebits & 0x80) != 0 && (swap->statebits & 0x80) != 0 )
            break;
        sleep(3 + (swap->iambob == 0)*10);
        basilisk_swapget(myinfo,swap,0x80000000,data,maxlen,basilisk_verify_otherstatebits);
        basilisk_sendstate(myinfo,swap,data,maxlen);
        if ( (swap->otherstatebits & 0x80) == 0 )
            basilisk_swapdata_rawtxsend(myinfo,swap,0x80,data,maxlen,&swap->myfee,0x40);
    }
    while ( retval == 0 && time(NULL) < swap->expiration )  // both sides have setup required data and paid txfee
    {
        if ( (rand() % 30) == 0 )
            printf("E r%u/q%u swapstate.%x otherstate.%x\n",swap->req.requestid,swap->req.quoteid,swap->statebits,swap->otherstatebits);
        if ( swap->iambob != 0 )
        {
            //printf("BOB\n");
            if ( (swap->statebits & 0x100) == 0 )
            {
                printf("send bobdeposit\n");
                swap->statebits |= basilisk_swapdata_rawtxsend(myinfo,swap,0x200,data,maxlen,&swap->bobdeposit,0x100);
            }
            // [BLOCKING: altfound] make sure altpayment is confirmed and send payment
            else if ( (swap->statebits & 0x1000) == 0 )
            {
                printf("check alicepayment\n");
                if ( basilisk_swapget(myinfo,swap,0x1000,data,maxlen,basilisk_verify_alicepaid) == 0 )
                {
                    swap->statebits |= 0x1000;
                    printf("got alicepayment\n");
                }
            }
            else if ( (swap->statebits & 0x2000) == 0 )
            {
                if ( basilisk_numconfirms(myinfo,&swap->alicepayment) >= swap->aliceconfirms )
                {
                    swap->statebits |= 0x2000;
                    printf("alicepayment confirmed\n");
                }
            }
            else if ( (swap->statebits & 0x4000) == 0 )
            {
                basilisk_bobscripts_set(myinfo,swap,0,1);
                printf("send bobpayment\n");
                swap->statebits |= basilisk_swapdata_rawtxsend(myinfo,swap,0x8000,data,maxlen,&swap->bobpayment,0x4000);
            }
            // [BLOCKING: privM] Bob waits for privAm either from Alice or alice blockchain
            else if ( (swap->statebits & 0x40000) == 0 )
            {
                if ( basilisk_swapget(myinfo,swap,0x40000,data,maxlen,basilisk_verify_privi) == 0 || basilisk_privAm_extract(myinfo,swap) == 0 ) // divulges privAm
                {
                    printf("got privi spend alicepayment\n");
                    swap->statebits |= 0x40000;
                    basilisk_alicepayment_spend(myinfo,swap,&swap->bobspend);
                    if ( basilisk_swapdata_rawtxsend(myinfo,swap,0,data,maxlen,&swap->bobspend,0x40000) == 0 )
                        printf("Bob error spending alice payment\n");
                    else
                    {
                        basilisk_swap_balancingtrade(myinfo,swap,1);
                        printf("Bob spends alicepayment\n");
                    }
                    break;
                }
                else if ( swap->bobpayment.locktime != 0 && time(NULL) > swap->bobpayment.locktime )
                {
                    // submit reclaim of payment
                    printf("bob reclaims bobpayment\n");
                    swap->statebits |= (0x40000 | 0x80000);
                    if ( basilisk_swapdata_rawtxsend(myinfo,swap,0,data,maxlen,&swap->bobreclaim,0) == 0 )
                        printf("Bob error reclaiming own payment after alice timed out\n");
                    else printf("Bob reclaimed own payment\n");
                    break;
                }
            }
            else if ( (swap->statebits & 0x80000) == 0 )
            {
                if ( basilisk_numconfirms(myinfo,&swap->bobspend) >= swap->aliceconfirms )
                {
                    printf("bobspend confirmed\n");
                    swap->statebits |= 0x80000 | 0x100000;
                    printf("Bob confirms spend of Alice's payment\n");
                    break;
                }
            }
            else if ( (swap->statebits & 0x100000) == 0 )
            {
                if ( basilisk_numconfirms(myinfo,&swap->bobreclaim) >= 1 )
                {
                    printf("bobreclaim confirmed\n");
                    swap->statebits |= 0x100000;
                    printf("Bob confirms reclain of payment\n");
                    break;
                }
            }
        }
        else
        {
            //printf("ALICE\n");
            // [BLOCKING: depfound] Alice waits for deposit to confirm and sends altpayment
            if ( (swap->statebits & 0x200) == 0 )
            {
                printf("checkfor deposit\n");
                if ( basilisk_swapget(myinfo,swap,0x200,data,maxlen,basilisk_verify_bobdeposit) == 0 )
                {
                    // verify deposit and submit, set confirmed height
                    printf("got bobdeposit\n");
                    swap->statebits |= 0x200;
                } else printf("no valid deposit\n");
            }
            else if ( (swap->statebits & 0x400) == 0 )
            {
                if ( basilisk_numconfirms(myinfo,&swap->bobdeposit) >= swap->bobconfirms )
                {
                    printf("bobdeposit confirmed\n");
                    swap->statebits |= 0x400;
                }
            }
            else if ( (swap->statebits & 0x800) == 0 )
            {
                printf("send alicepayment\n");
                swap->statebits |= basilisk_swapdata_rawtxsend(myinfo,swap,0x1000,data,maxlen,&swap->alicepayment,0x800);
            }
            // [BLOCKING: payfound] make sure payment is confrmed and send in spend or see bob's reclaim and claim
            else if ( (swap->statebits & 0x8000) == 0 )
            {
                if ( basilisk_swapget(myinfo,swap,0x8000,data,maxlen,basilisk_verify_bobpaid) == 0 )
                {
                    printf("got bobpayment\n");
                    basilisk_swap_balancingtrade(myinfo,swap,0);
                    // verify payment and submit, set confirmed height
                    swap->statebits |= 0x8000;
                }
            }
            else if ( (swap->statebits & 0x10000) == 0 )
            {
                if ( basilisk_numconfirms(myinfo,&swap->bobpayment) >= swap->bobconfirms )
                {
                    printf("bobpayment confirmed\n");
                    swap->statebits |= 0x10000;
                }
            }
            else if ( (swap->statebits & 0x20000) == 0 )
            {
                printf("alicespend bobpayment\n");
                if ( basilisk_swapdata_rawtxsend(myinfo,swap,0,data,maxlen,&swap->alicespend,0x20000) != 0 && basilisk_numconfirms(myinfo,&swap->alicespend) > 0 )
                {
                    for (j=datalen=0; j<32; j++)
                        data[datalen++] = swap->privAm.bytes[j];
                    printf("send privAm\n");
                    swap->statebits |= basilisk_swapsend(myinfo,swap,0x40000,data,datalen,0x20000,swap->crcs_mypriv);
                }
            }
            else if ( (swap->statebits & 0x40000) == 0 )
            {
                if ( basilisk_numconfirms(myinfo,&swap->alicespend) >= swap->bobconfirms )
                {
                    swap->statebits |= 0x40000;
                    printf("Alice confirms spend of Bob's payment\n");
                    break;
                }
            }
            if ( swap->bobdeposit.locktime != 0 && time(NULL) > swap->bobdeposit.locktime )
            {
                printf("Alice claims deposit\n");
                if ( basilisk_swapdata_rawtxsend(myinfo,swap,0,data,maxlen,&swap->aliceclaim,0) == 0 )
                    printf("Alice couldnt claim deposit\n");
                else printf("Alice claimed deposit\n");
                break;
            }
            else if ( basilisk_privBn_extract(myinfo,swap,data,maxlen) == 0 )
            {
                printf("Alice reclaims her payment\n");
                swap->statebits |= 0x40000000;
                if ( basilisk_swapdata_rawtxsend(myinfo,swap,0,data,maxlen,&swap->alicereclaim,0x40000000) == 0 )
                    printf("Alice error sending alicereclaim\n");
                else printf("Alice reclaimed her payment\n");
                break;
            }
        }
        if ( (rand() % 30) == 0 )
            printf("finished swapstate.%x other.%x\n",swap->statebits,swap->otherstatebits);
        sleep(1);//3 + (swap->iambob == 0));
        basilisk_sendstate(myinfo,swap,data,maxlen);
        basilisk_swapget(myinfo,swap,0x80000000,data,maxlen,basilisk_verify_otherstatebits);
    }
    printf("end of atomic swap\n");
    if ( swap->iambob != 0 )//&& bits256_nonz(swap->bobdeposit.txid) != 0 )
    {
        printf("BOB reclaims refund\n");
        basilisk_bobdeposit_refund(myinfo,swap,0);
        if ( basilisk_swapdata_rawtxsend(myinfo,swap,0,data,maxlen,&swap->bobrefund,0x40000000) == 0 ) // use secretBn
        {
            printf("Bob submit error getting refund of deposit\n");
        }
        // maybe wait for bobrefund to be confirmed
        for (j=datalen=0; j<32; j++)
            data[datalen++] = swap->privBn.bytes[j];
        basilisk_swapsend(myinfo,swap,0x40000000,data,datalen,0x40000000,swap->crcs_mypriv);
    }
    printf("%s swap finished statebits %x\n",swap->iambob!=0?"BOB":"ALICE",swap->statebits);
    basilisk_swap_purge(myinfo,swap);
}

struct basilisk_swap *basilisk_thread_start(struct supernet_info *myinfo,struct basilisk_request *rp)
{
    int32_t i; struct basilisk_swap *swap = 0;
    portable_mutex_lock(&myinfo->DEX_swapmutex);
    for (i=0; i<myinfo->numswaps; i++)
        if ( myinfo->swaps[i]->req.requestid == rp->requestid )
        {
            printf("basilisk_thread_start error trying to start requestid.%u which is already started\n",rp->requestid);
            break;
        }
    if ( i == myinfo->numswaps && i < sizeof(myinfo->swaps)/sizeof(*myinfo->swaps) )
    {
        swap = calloc(1,sizeof(*swap));
        vcalc_sha256(0,swap->orderhash.bytes,(uint8_t *)rp,sizeof(*rp));
        swap->req = *rp;
        swap->myinfo = myinfo;
        printf("START swap requestid.%u\n",rp->requestid);
        if ( bitcoin_swapinit(myinfo,swap) != 0 )
        {
            fprintf(stderr,"launch.%d %d\n",myinfo->numswaps,(int32_t)(sizeof(myinfo->swaps)/sizeof(*myinfo->swaps)));
            if ( OS_thread_create(malloc(sizeof(pthread_t)),NULL,(void *)basilisk_swaploop,(void *)swap) != 0 )
            {
                
            }
            myinfo->swaps[myinfo->numswaps++] = swap;
        }
    }
    portable_mutex_unlock(&myinfo->DEX_swapmutex);
    return(swap);
}
