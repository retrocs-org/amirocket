<?php
require_once __DIR__.'/../includes/releases.php';
$count = 0;
function check(bool $value): void { global $count; $count++; if (!$value) throw new RuntimeException('Check '.$count.' failed'); }
$base = AR_RELEASE_REPO.'/releases/download/v0.2/';
$record = ['tag_name'=>'v0.2', 'draft'=>false, 'prerelease'=>true, 'published_at'=>'2026-11-18T12:00:00Z', 'assets'=>[]];
foreach (['AmiRocket-0.2.lha','AmiRocket-0.2.lha.sha256','AmiRocket-0.2-plugin-sdk.zip','AmiRocket-0.2-source.zip'] as $name) $record['assets'][]=['name'=>$name,'browser_download_url'=>$base.$name,'size'=>1024,'state'=>'uploaded','download_count'=>3];
$entry = ar_release_normalize($record);
check($entry['version']==='0.2'); check($entry['prerelease']); check($entry['binary_url']===$base.'AmiRocket-0.2.lha');
check($entry['source_url']===$base.'AmiRocket-0.2-source.zip'); check($entry['checksums_url']===$base.'AmiRocket-0.2.lha.sha256');
check($entry['binary_download_count']===3); check($entry['sdk_download_count']===3); check($entry['source_download_count']===3);
check(ar_release_asset_meta($entry,'binary')==='1 KB · 3 downloads on GitHub');
$zero=$entry; $zero['binary_download_count']=0; check(str_contains(ar_release_asset_meta($zero,'binary'),'0 downloads on GitHub'));
$missing=$entry; $missing['binary_download_count']=null; check(ar_release_asset_meta($missing,'binary')==='1 KB');
$badCount=$record; $badCount['assets'][0]['download_count']=-1; check(ar_release_normalize($badCount)['binary_download_count']===null);
$badCount['assets'][0]['download_count']='123'; check(ar_release_normalize($badCount)['binary_download_count']===null);
$pending=$record; $pending['assets'][0]['state']='new'; check(ar_release_normalize($pending)['binary_url']===null);
$pending['assets'][0]['state']='uploaded'; $pending['assets'][0]['size']=0; check(ar_release_normalize($pending)['binary_url']===null);
$draft=$record; $draft['draft']=true; check(ar_release_normalize($draft)===null);
$bad=$record; $bad['assets'][0]['browser_download_url']='https://example.com/payload'; check(ar_release_normalize($bad)['binary_url']===null);
$empty=$record; $empty['assets']=[]; check(ar_release_normalize($empty)['binary_url']===null); check(ar_release_normalize($empty)['source_url']===null);
$unsafe=$record; $unsafe['tag_name']="bad\r\nheader"; $unsafe['assets']=[]; check(ar_release_normalize($unsafe)===null);
$temp=sys_get_temp_dir().'/ar-web-test-'.bin2hex(random_bytes(6)); mkdir($temp,0700); $fallback=$temp.'/fallback.json'; file_put_contents($fallback,'[]');
try {
 $calls=0; $fetch=function($etag) use (&$calls,$record) {$calls++; return ['status'=>200,'etag'=>'"test"','records'=>[$record]];};
 $catalog=ar_release_catalog($fetch,$temp,1000,$fallback); check(count($catalog['releases'])===1); check(!$catalog['stale']); check($calls===1);
 $cached=ar_release_catalog($fetch,$temp,1001,$fallback); check($calls===1); check($cached['source']==='cache'); check($cached['releases'][0]['source_download_count']===3); check($cached['releases'][0]['binary_download_count']===3);
 $failed=ar_release_catalog(fn($etag)=>null,$temp,2000,$fallback); check($failed['stale']); check($failed['releases'][0]['binary_url']===$entry['binary_url']);
 $empty=ar_release_catalog(fn($etag)=>['status'=>200,'etag'=>'','records'=>[]],$temp,2100,$fallback); check($empty['releases']===[]); check(!$empty['stale']);
 $notModified=ar_release_catalog(fn($etag)=>['status'=>304,'etag'=>$etag],$temp,3100,$fallback); check($notModified['releases']===[]); check(!$notModified['stale']);
} finally { foreach(glob($temp.'/*') as $file) unlink($file); rmdir($temp); }
echo "$count release catalogue checks passed.\n";
