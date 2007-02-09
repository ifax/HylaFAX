# We need HTMLizing certain fields 

html_sanitize()
{
    for i in $*
    do
	#printf '%s: \n' "$i"
	val=`eval echo "$""$i" | sed -e 's/</\&lt;/g;s/>/\&gt;/g'`
	eval "$i"_HTML='"'"$val"'"'
	eval export "$i"_HTML
    done
}

html_sanitize SENDTO
