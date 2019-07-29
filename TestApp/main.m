//
//  main.m
//  TestApp
//
//  Created by Jesús A. Álvarez on 06/07/2019.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import <Foundation/Foundation.h>

int main(int argc, char * argv[]) {
    @autoreleasepool {
        //NSLog(@"Hello, I'm a test app %d %d %g", 1, 2, 3.0);
        NSDictionary *dictionary = [[NSDictionary alloc] initWithObjectsAndKeys:@"object", @"key", @"object", @"key2", nil];
        NSLog(@"The dictionary is %@", dictionary);
        
        NSString *string = @"This is a string";
        NSString *substring = [string substringWithRange:NSMakeRange(5, 2)];
        NSLog(@"The substring is “%@”", substring);
        
        NSRange range = [string rangeOfString:@"is"];
        NSLog(@"The range is {%d, %d}", (int)range.location, (int)range.length);
        return 4;
    }
}
