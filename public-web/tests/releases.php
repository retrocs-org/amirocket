<?php
require_once __DIR__.'/../includes/releases.php';
$count = 0;
function check(bool $value): void { global $count; $count++; if (!$value) throw new RuntimeException('Check '.$count.' failed'); }
$base = AR_RELEASE_REPO.'/releases/download/v0.2/';
$record = ['tag_name'=>'v0.2', 'draft'=>false, 'prerelease'=>true, 'published_at'=>'2026-11-18T12:00:00Z', 'assets'=>[]];
foreach (['AmiRocket-0.2.lha','AmiRocket-0.2.lha.sha256','AmiRocket-0.2-plugin-sdk.zip','AmiRocket-0.2-source.zip'] as $name) $record['assets'][]=['name'=>$name,'browser_download_url'=>$base.$name,'size'=>1024,'download_count'=>3];
$entry = ar_release_normalize($record);
check($entry['version']==='0.2'); check($entry['prerelease']); check($entry['binary_url']===$base.'AmiRocket-0.2.lha');
check($entry['source_url']===$base.'AmiRocket-0.2-source.zip'); check($entry['checksums_url']===$base.'AmiRocket-0.2.lha.sha256');
$draft=$record; $draft['draft']=true; check(ar_release_normalize($draft)===null);
$bad=$record; $bad['assets'][0]['browser_download_url']='https://example.com/payload'; check(ar_release_normalize($bad)['binary_url']===null);
$empty=$record; $empty['assets']=[]; check(ar_release_normalize($empty)['binary_url']===null); check(ar_release_normalize($empty)['source_url']===null);
$unsafe=$record; $unsafe['tag_name']="bad\r\nheader"; $unsafe['assets']=[]; check(ar_release_normalize($unsafe)===null);
$temp=sys_get_temp_dir().'/ar-web-test-'.bin2hex(random_bytes(6)); mkdir($temp,0700); $fallback=$temp.'/fallback.json'; file_put_contents($fallback,'[]');
try {
 $calls=0; $fetch=function($etag) use (&$calls,$record) {$calls++; return ['status'=>200,'etag'=>'"test"','records'=>[$record]];};
 $catalog=ar_release_catalog($fetch,$temp,1000,$fallback); check(count($catalog['releases'])===1); check(!$catalog['stale']); check($calls===1);
 $cached=ar_release_catalog($fetch,$temp,1001,$fallback); check($calls===1); check($cached['source']==='cache');
 $failed=ar_release_catalog(fn($etag)=>null,$temp,2000,$fallback); check($failed['stale']); check($failed['releases'][0]['binary_url']===$entry['binary_url']);
 $empty=ar_release_catalog(fn($etag)=>['status'=>200,'etag'=>'','records'=>[]],$temp,2100,$fallback); check($empty['releases']===[]); check(!$empty['stale']);
 $notModified=ar_release_catalog(fn($etag)=>['status'=>304,'etag'=>$etag],$temp,3100,$fallback); check($notModified['releases']===[]); check(!$notModified['stale']);
} finally { foreach(glob($temp.'/*') as $file) unlink($file); rmdir($temp); }
echo "$count release catalogue checks passed.\n";
